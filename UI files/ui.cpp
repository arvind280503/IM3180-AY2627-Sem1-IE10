#include "ui.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
constexpr float topFlipX = 740.f;
constexpr float topSettingsX = 908.f;
constexpr float topProfileX = 1086.f;
constexpr float topProfileRadius = 27.f;
constexpr int promotionPieces[] = {5, 4, 3, 2};
constexpr SpecialMove promotionMoves[] = {SpecialMove::PromoteQueen, SpecialMove::PromoteRook, SpecialMove::PromoteBishop, SpecialMove::PromoteKnight};

void loadDefaultProfilePicture(sf::Texture& texture, std::string& path) {
    path = "assets/pfp/default_pfp.jpe";
    static_cast<void>(texture.loadFromFile(path));
}

}

ChessUI::ChessUI(int engineSide, int searchDepth, std::unique_ptr<Evaluator> whiteEvaluator, std::unique_ptr<Evaluator> blackEvaluator)
: game(),
whiteEngine(0, searchDepth, std::move(whiteEvaluator)),
blackEngine(1, searchDepth, std::move(blackEvaluator)),
engineSide(engineSide),
window( sf::VideoMode({1200, 800}), "Chess AI", sf::Style::Default, sf::State::Windowed), 
engineSearchDepth(searchDepth), startingBoard(game) {
    if(searchDepth < 1) throw std::invalid_argument("searchDepth must be positive");
    window.setFramerateLimit(60);
    updateResponsiveView(window.getSize().x, window.getSize().y);
    loadDefaultProfilePicture(profilePictureTexture, profilePicturePath);
    refreshPieceThemes();
    loadPieceTextures();
    refreshBoardThemes();
    loadBoardTextures();


    static_cast<void>(font.openFromFile("assets/fonts/font.ttf"));
    static_cast<void>(startBackgroundTexture.loadFromFile("assets/design/start_background.png"));
    static_cast<void>(selectionFrameTexture.loadFromFile("assets/design/selection_frames/frame001.jpg"));
    static_cast<void>(moveSoundBuffer.loadFromFile("assets/sounds/move.wav"));
    static_cast<void>(captureSoundBuffer.loadFromFile("assets/sounds/capture.wav"));
    static_cast<void>(uiClickSoundBuffer.loadFromFile("assets/sounds/ui_click.wav"));
    static_cast<void>(victorySoundBuffer.loadFromFile("assets/sounds/victory.wav"));
}

ChessUI::~ChessUI() {
    if (searchStop) searchStop->store(true, std::memory_order_relaxed);
    if (searchFuture.valid()) searchFuture.wait();
}

void ChessUI::run() {
    while (window.isOpen()) {
        handleEvents();
        if (!window.isOpen()) break;
        if (historyHorizontalScrollbarDragging) {
            if (sf::Mouse::isButtonPressed(sf::Mouse::Button::Left)) {
                const sf::Vector2i pixelPosition = sf::Mouse::getPosition(window);
                scrollHistoryHorizontalFromMouse(window.mapPixelToCoords(pixelPosition, logicalView).x);
            }
            else historyHorizontalScrollbarDragging = false;
        }
        if (historyPreviewHeld && !sf::Mouse::isButtonPressed(sf::Mouse::Button::Left))
            historyPreviewHeld = false;
        draw();
        updateEngine();
        updateClocks();
        updateEvaluationBar();
    }
}

bool ChessUI::applyMove(const Move& requested) {
    if (!game.valid_move(requested.old_x, requested.old_y, requested.new_x, requested.new_y))
        return false;

    historyBrowsing = false;

    const int fromRow = requested.old_x;
    const int fromCol = requested.old_y;
    const int toRow = requested.new_x;
    const int toCol = requested.new_y;

    const int movedPiece = (*(game.begin() + fromRow))[fromCol];
    const int capturedPiece = (*(game.begin() + toRow))[toCol];
    const auto availableMoves = game.legal_moves();
    const bool promotionMove = std::any_of(availableMoves.begin(), availableMoves.end(), [&requested](const Move& move) {
        return move.old_x == requested.old_x && move.old_y == requested.old_y && move.new_x == requested.new_x && move.new_y == requested.new_y && move.special_move >= SpecialMove::PromoteQueen && move.special_move <= SpecialMove::PromoteKnight;
    });

    if (promotionMove && requested.special_move == SpecialMove::None && !engineTurn()) {
        pendingPromotion = requested;
        currentScreen = Screen::Promotion;
        return true;
    }

    const bool humanMove = game.get_current_turn() != engineSide;
    if (!game.make_move(requested)) return false;
    invalidateSearch();
    if (humanMove) clockStarted = true;
    if (promotionMove) pendingPromotion = Move{};
    const int entryPromotionPiece = promotionMove ? (*(game.begin() + toRow))[toCol] : 0;

    MoveHistoryEntry entry{
        movedPiece,
        fromRow,
        fromCol,
        toRow,
        toCol,
        capturedPiece,
        entryPromotionPiece,
        game
    };

    int newNode = -1;
    for (int i = 0; i < static_cast<int>(historyTree.size()) && !forceNewBranch; i++) {
        const MoveHistoryEntry& existing = historyTree[i].move;
        if (historyTree[i].parent == currentHistoryNode &&
            existing.fromRow == fromRow && existing.fromCol == fromCol &&
            existing.toRow == toRow && existing.toCol == toCol &&
            existing.promotionPiece == entryPromotionPiece) {
            newNode = i;
            break;
        }
    }

    if (newNode < 0) {
        newNode = static_cast<int>(historyTree.size());
        historyTree.push_back({entry, currentHistoryNode, {}});

        if (currentHistoryNode >= 0) {
            historyTree[currentHistoryNode].children.push_back(newNode);
        }
    }

    forceNewBranch = false;
    currentHistoryNode = newNode;
    viewingHistoryNode = newNode;
    engineMoveWaiting = true;
    engineMoveClock.restart();
    engineMoveAnimating = false;

    rebuildLinearHistoryFromNode(currentHistoryNode);
    historyScroll = getHistoryScrollBar().maxScroll;
    const float newNodeX = getHistoryNodePositions()[newNode].x;
    const float historyLeft = sidePanelX + 20.f;
    const float historyRight = getHistoryScrollBar().x - 8.f;
    if (newNodeX < historyLeft) historyHorizontalScroll += newNodeX - historyLeft;
    else if (newNodeX + 45.f > historyRight) historyHorizontalScroll += newNodeX + 45.f - historyRight;
    historyHorizontalScroll = std::clamp(historyHorizontalScroll, 0.f, getHistoryHorizontalScrollBar().maxScroll);

    legalMoves.clear();
    clearSelection();

    playSound(capturedPiece != 0 ? captureSoundBuffer : moveSoundBuffer);

    if (game.has_game_ended()) {
        recordCompletedMatch();
        playSound(victorySoundBuffer);
        currentScreen = Screen::EndGame;
    }

    return true;
}

bool ChessUI::engineTurn() const {
    if (game.has_game_ended()) return false;
    return game.get_current_turn() == engineSide;
}

void ChessUI::updateEngine() {
    if (currentScreen == Screen::Game && engineMoveAnimating) {
        if (engineAnimationClock.getElapsedTime().asSeconds() < engineSlideSeconds)
            return;

        engineMoveAnimating = false;
        animatedPiece = 0;

        if (!applyMove(animatedMove))
            engineStalled = true;
        return;
    }

    if (searchFuture.valid()) {
        const bool obsolete = searchGeneration != positionGeneration ||
                              searchRootHash != game.get_hash();
        if (obsolete) searchStop->store(true, std::memory_order_relaxed);
        if (searchFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;
        if (searchStop->load(std::memory_order_relaxed)) {
            searchFuture = {};
            searchStop.reset();
            return;
        }
    }

    if (currentScreen != Screen::Game) return;

    if (historyBrowsing)
        return;

    if (game.has_game_ended() || engineStalled || !engineTurn()) return;

    if (!engineMoveWaiting) {
        engineMoveWaiting = true;
        engineMoveClock.restart();
        return;
    }

    if (engineMoveClock.getElapsedTime().asSeconds() < engineMoveDelaySeconds)
        return;

    if (currentHistoryIndex + 1 <
        static_cast<int>(moveHistory.size())) {
        return;
    }

    SearchResult result{};
    try {
        if (!searchFuture.valid()) {
            searchStop = std::make_shared<std::atomic<bool>>(false);
            searchGeneration = positionGeneration;
            searchRootHash = game.get_hash();
            Engine& engine = (game.get_current_turn() == 0) ? whiteEngine : blackEngine;
            searchFuture = std::async(std::launch::async,
                [snapshot = game, &engine, depth = engineSearchDepth, stop = searchStop] {
                    const bool found = engine.find_best_move(snapshot, depth, stop.get());
                    return SearchResult{found, found ? engine.get_best_move() : Move{}};
                });
            return;
        }
        result = searchFuture.get();
        searchStop.reset();
    } catch (...) {
        searchStop.reset();
        engineStalled = true;
        engineSearchFailed = true;
        return;
    }

    if (!result.found) {
        engineStalled = true;
        return;
    }

    animatedMove = result.move;
    const int fromRow = animatedMove.old_x;
    const int fromCol = animatedMove.old_y;
    const int toRow = animatedMove.new_x;
    const int toCol = animatedMove.new_y;

    animatedFromRow = fromRow;
    animatedFromCol = fromCol;
    animatedToRow = toRow;
    animatedToCol = toCol;
    animatedPiece = (*(game.begin() + fromRow))[fromCol];
    engineMoveAnimating = true;
    engineMoveWaiting = false;
    engineAnimationClock.restart();
}

void ChessUI::updateResponsiveView(unsigned int width, unsigned int height) {
    if (width == 0 || height == 0)
        return;

    const float logicalWidth = static_cast<float>(windowWidth);
    const float logicalHeight = static_cast<float>(windowHeight);
    const float targetAspect = logicalWidth / logicalHeight;
    const float actualAspect = static_cast<float>(width) / static_cast<float>(height);

    sf::FloatRect viewport;
    if (actualAspect > targetAspect) {
        const float viewportWidth = targetAspect / actualAspect;
        viewport = sf::FloatRect(
            {(1.f - viewportWidth) / 2.f, 0.f},
            {viewportWidth, 1.f});
    }
    else {
        const float viewportHeight = actualAspect / targetAspect;
        viewport = sf::FloatRect(
            {0.f, (1.f - viewportHeight) / 2.f},
            {1.f, viewportHeight});
    }

    logicalView = sf::View(sf::FloatRect(
        {0.f, 0.f}, {logicalWidth, logicalHeight}));
    logicalView.setViewport(viewport);
    window.setView(logicalView);
}

void ChessUI::toggleFullscreen() {
    fullscreen = !fullscreen;
    if (fullscreen) {
        window.create( sf::VideoMode::getDesktopMode(), "Chess AI", sf::State::Fullscreen);
    }
    else {
        window.create( sf::VideoMode({static_cast<unsigned int>(windowWidth), static_cast<unsigned int>(windowHeight)}), "Chess AI", sf::Style::Default, sf::State::Windowed);
    }
    window.setFramerateLimit(60);
    updateResponsiveView(window.getSize().x, window.getSize().y);
}

void ChessUI::updateClocks() {
    if (currentScreen != Screen::Game || !gameStarted || !clockStarted ||
        game.has_game_ended() || timeExpired || historyBrowsing) {
        clockTimer.restart();
        return;
    }

    const int turn = game.get_current_turn();
    const int movesLeft = game.get_moves_left();

    if (clockKeyNode != currentHistoryNode || clockKeyTurn != turn ||
        clockKeyMovesLeft != movesLeft) {
        clockKeyNode = currentHistoryNode;
        clockKeyTurn = turn;
        clockKeyMovesLeft = movesLeft;
        moveTimeLeft = matchSeconds;
        clockTimer.restart();
        return;
    }

    float dt = clockTimer.getElapsedTime().asSeconds();
    clockTimer.restart();
    if (dt > 5.f) dt = 0.f;

    moveTimeLeft -= dt;
    if (moveTimeLeft <= 0.f) {
        moveTimeLeft = 0.f;
        timeExpired = true;
        timeoutLoser = turn;

        dragging = false;
        draggedRow = -1;
        draggedCol = -1;
        legalMoves.clear();
        clearSelection();
        engineMoveAnimating = false;
        engineMoveWaiting = false;

        recordCompletedMatch();
        playSound(victorySoundBuffer);
        currentScreen = Screen::EndGame;
    }
}

void ChessUI::drawClocks() {
    if (!gameStarted) return;
    if (currentScreen != Screen::Game && currentScreen != Screen::EndGame) return;

    const int turn = game.get_current_turn();

    auto drawBox = [&](float x, const std::string& label, bool active) {
        const float seconds = active && !historyBrowsing ? std::max(0.f, moveTimeLeft) : matchSeconds;
        const int whole = static_cast<int>(seconds);
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "%02d:%02d", whole / 60, whole % 60);

        sf::RectangleShape box({108.f, 26.f});
        box.setPosition({x, 6.f});
        box.setFillColor(active ? sf::Color(63, 128, 78) : sf::Color(55, 55, 55));
        box.setOutlineColor(active ? sf::Color(91, 157, 104) : sf::Color(104, 108, 114));
        box.setOutlineThickness(1.f);
        window.draw(box);

        sf::Text text(font, label + " " + buffer, 13);
        text.setPosition({x + 6.f, 12.f});
        text.setFillColor(active && seconds <= 15.f ? sf::Color(255, 120, 120) : sf::Color::White);
        window.draw(text);
    };

    drawBox(445.f, "W", turn == 0 && !timeExpired);
    drawBox(560.f, "B", turn == 1 && !timeExpired);
}

void ChessUI::handleEvents() {
    while (const std::optional event = window.pollEvent()) {
        if (const auto* resized = event->getIf<sf::Event::Resized>()) {
            updateResponsiveView(resized->size.x, resized->size.y);
        }

        if (const auto* keyPressed = event->getIf<sf::Event::KeyPressed>()) {
            if (keyPressed->code == sf::Keyboard::Key::F11) {
                toggleFullscreen();
                continue;
            }

            if (keyPressed->code == sf::Keyboard::Key::Escape && fullscreen) {
                toggleFullscreen();
                continue;
            }

            if (currentScreen == Screen::Account) {
                if (accountSignedIn && profilePicturePickerOpen && keyPressed->code == sf::Keyboard::Key::Escape) {
                    profilePicturePickerOpen = false;
                    continue;
                }
                if (keyPressed->code == sf::Keyboard::Key::Backspace) {
                    if (!accountSignedIn) {
                        std::string& input = accountPasswordField
                            ? accountPassword : accountInput;
                        if (!input.empty()) input.pop_back();
                    }
                }
                else if (keyPressed->code == sf::Keyboard::Key::Enter) {
                    if (!accountSignedIn)
                        submitAccountForm();
                }
                continue;
            }

            if (currentScreen == Screen::Promotion) {
                SpecialMove chosenMove = SpecialMove::None;
                if (keyPressed->code == sf::Keyboard::Key::Q) chosenMove = SpecialMove::PromoteQueen;
                else if (keyPressed->code == sf::Keyboard::Key::R) chosenMove = SpecialMove::PromoteRook;
                else if (keyPressed->code == sf::Keyboard::Key::B) chosenMove = SpecialMove::PromoteBishop;
                else if (keyPressed->code == sf::Keyboard::Key::N) chosenMove = SpecialMove::PromoteKnight;

                Move promotion = pendingPromotion;
                promotion.special_move = chosenMove;
                if (chosenMove != SpecialMove::None && applyMove(promotion) && currentScreen == Screen::Promotion) {
                    currentScreen = Screen::Game;
                }
                continue;
            }
        }

        if (const auto* textEntered = event->getIf<sf::Event::TextEntered>()) {
            if (currentScreen == Screen::Account && !accountSignedIn &&  textEntered->unicode >= 32 && textEntered->unicode <= 126) {
                std::string& input = accountPasswordField ? accountPassword : accountInput;
                if (input.size() < 40)
                    input.push_back(static_cast<char>(textEntered->unicode));
            }
        }

        if (const auto* wheel = event->getIf<sf::Event::MouseWheelScrolled>())
        {
            const sf::Vector2f logicalMouse = window.mapPixelToCoords(wheel->position, logicalView);
            float mouseX = logicalMouse.x;
            float mouseY = logicalMouse.y;

            if (currentScreen == Screen::Game && mouseX >= sidePanelX && mouseX <= sidePanelX + sidePanelWidth && mouseY >= sidePanelY && mouseY <= sidePanelY + sidePanelHeight)
            {
                historyScroll = std::clamp(historyScroll - wheel->delta * 80.f, 0.f, getHistoryScrollBar().maxScroll);
            }

            if (currentScreen == Screen::Account && accountSignedIn && profilePicturePickerOpen && mouseX >= 200.f && mouseX <= 1000.f && mouseY >= 230.f && mouseY <= 650.f) {
                const float contentHeight = static_cast<float>((profilePictureOptions.size() + 5) / 6) * 92.f;
                const float maxScroll = std::max(0.f, contentHeight - 400.f);
                profilePicturePickerScroll = std::clamp( profilePicturePickerScroll - wheel->delta * 92.f, 0.f, maxScroll);
            }
        }

        if (event->is<sf::Event::Closed>()) {
            invalidateSearch();
            window.close();
        }

        if (const auto* mousePressed = event->getIf<sf::Event::MouseButtonPressed>()) {
            if (mousePressed->button == sf::Mouse::Button::Left) {
                const sf::Vector2f logicalMouse = window.mapPixelToCoords(mousePressed->position, logicalView);
                float mouseX = logicalMouse.x;
                float mouseY = logicalMouse.y;

                if (currentScreen == Screen::Game) {
                    const HistoryHorizontalScrollBar horizontalBar = getHistoryHorizontalScrollBar();
                    if (horizontalBar.maxScroll > 0.f && mouseX >= horizontalBar.x - 8.f && mouseX <= horizontalBar.x + horizontalBar.width + 8.f && mouseY >= horizontalBar.y - 9.f && mouseY <= horizontalBar.y + 10.f) {
                        historyHorizontalScrollbarDragging = true;
                        historyHorizontalScrollbarGrabOffset = mouseX >= horizontalBar.thumbX && mouseX <= horizontalBar.thumbX + horizontalBar.thumbWidth ? mouseX - horizontalBar.thumbX : horizontalBar.thumbWidth / 2.f;
                        scrollHistoryHorizontalFromMouse(mouseX);
                        continue;
                    }
                }

                const bool profileButtonAvailable = currentScreen != Screen::StartScreen && currentScreen != Screen::Settings && currentScreen != Screen::Account && currentScreen != Screen::Promotion;
                if (profileButtonAvailable && mouseX >= topProfileX && mouseX <= topProfileX + topProfileRadius * 2.f && mouseY >= 13.f && mouseY <= 13.f + topProfileRadius * 2.f) {
                    playSound(uiClickSoundBuffer);
                    accountReturnScreen = currentScreen;
                    accountMessage.clear();
                    profilePicturePickerOpen = false;
                    if (!accountSignedIn)
                        accountSignUpMode = false;
                    currentScreen = Screen::Account;
                    continue;
                }

                if (currentScreen == Screen::Account) {
                    if (accountSignedIn) {
                        if (profilePicturePickerOpen) {
                            if (mouseX >= 930.f && mouseX <= 975.f &&
                                mouseY >= 135.f && mouseY <= 180.f) {
                                profilePicturePickerOpen = false;
                            } else if (mouseX >= 285.f && mouseX < 933.f &&
                                       mouseY >= 245.f && mouseY < 645.f) {
                                constexpr float cardWidth = 90.f;
                                constexpr float cardHeight = 82.f;
                                constexpr float stepX = 108.f;
                                constexpr float stepY = 92.f;
                                constexpr int columns = 6;
                                const int col = static_cast<int>(
                                    (mouseX - 285.f) / stepX);
                                const int row = static_cast<int>(
                                    (mouseY - 245.f + profilePicturePickerScroll) / stepY);
                                const int index = row * columns + col;
                                const float cardX = 285.f + col * stepX;
                                const float cardY = 245.f + row * stepY
                                    - profilePicturePickerScroll;
                                if (col < columns && mouseX < cardX + cardWidth &&
                                    mouseY >= cardY && mouseY < cardY + cardHeight &&
                                    index >= 0 && index < static_cast<int>(profilePictureOptions.size())) {
                                    profilePicturePath = profilePictureOptions[index];
                                    static_cast<void>(profilePictureTexture.loadFromFile(profilePicturePath));
                                    profilePicturePickerOpen = false;
                                }
                            }
                            else if (mouseX < 200.f || mouseX > 1000.f ||
                                     mouseY < 120.f || mouseY > 710.f) {
                                profilePicturePickerOpen = false;
                            }
                            continue;
                        }

                        if (mouseX >= 556.f && mouseX <= 644.f &&
                            mouseY >= 162.f && mouseY <= 250.f) {
                            playSound(uiClickSoundBuffer);
                            loadProfilePictureOptions();
                            profilePicturePickerScroll = 0.f;
                            profilePicturePickerOpen = true;
                        }
                        else if (mouseX >= 430.f && mouseX <= 770.f &&
                            mouseY >= 430.f && mouseY <= 482.f) {
                            playSound(uiClickSoundBuffer);
                            openMatchHistory();
                        }
                        else if (mouseX >= 430.f && mouseX <= 770.f &&
                                 mouseY >= 495.f && mouseY <= 547.f) {
                            playSound(uiClickSoundBuffer);
                            if (logoutHandler) {
                                try {
                                    logoutHandler();
                                }
                                catch (...) {
                                    accountMessage = "Could not complete sign out.";
                                    continue;
                                }
                            }
                            accountSignedIn = false;
                            accountName.clear();
                            accountInput.clear();
                            accountPassword.clear();
                            accountSignUpMode = false;
                            profilePicturePickerOpen = false;
                            loadDefaultProfilePicture(
                                profilePictureTexture, profilePicturePath);
                            matchSummaries.clear();
                            matchHistoryMessage.clear();
                            currentScreen = accountReturnScreen;
                        }
                        else if (mouseX >= 500.f && mouseX <= 700.f &&
                                 mouseY >= 585.f && mouseY <= 629.f) {
                            profilePicturePickerOpen = false;
                            currentScreen = accountReturnScreen;
                        }
                        continue;
                    }

                    if (mouseX >= 370.f && mouseX <= 830.f &&
                        mouseY >= 250.f && mouseY <= 304.f) {
                        accountPasswordField = false;
                    }
                    else if (mouseX >= 370.f && mouseX <= 830.f &&
                             mouseY >= 349.f && mouseY <= 403.f) {
                        accountPasswordField = true;
                    }
                    else if (mouseX >= 370.f && mouseX <= 830.f &&
                             mouseY >= 425.f && mouseY <= 473.f) {
                        accountSignUpMode = !accountSignUpMode;
                        accountMessage.clear();
                    }
                    else if (mouseX >= 430.f && mouseX <= 770.f &&
                             mouseY >= 530.f && mouseY <= 586.f) {
                        submitAccountForm();
                    }
                    else if (mouseX >= 500.f && mouseX <= 700.f &&
                             mouseY >= 620.f && mouseY <= 670.f) {
                        currentScreen = accountReturnScreen;
                    }
                    continue;
                }

                if (currentScreen == Screen::MatchHistory) {
                    if (replayActive) {
                        if (mouseY >= 730.f && mouseY <= 764.f) {
                            if (mouseX >= 275.f && mouseX <= 365.f) stepReplay(-1);
                            else if (mouseX >= 555.f && mouseX <= 645.f) stepReplay(1);
                            else if (mouseX >= 665.f && mouseX <= 755.f) replayActive = false;
                            else continue;
                            playSound(uiClickSoundBuffer);
                        }
                        continue;
                    }

                    const int pageCount = std::max(1, static_cast<int>(
                        (matchSummaries.size() + matchHistoryRowsPerPage - 1) / matchHistoryRowsPerPage));
                    if (mouseY >= 602.f && mouseY <= 636.f &&
                        ((mouseX >= 275.f && mouseX <= 365.f) || (mouseX >= 555.f && mouseX <= 645.f))) {
                        matchHistoryPage = mouseX <= 365.f
                            ? std::max(0, matchHistoryPage - 1)
                            : std::min(pageCount - 1, matchHistoryPage + 1);
                        playSound(uiClickSoundBuffer);
                        continue;
                    }

                    const std::size_t pageStart =
                        static_cast<std::size_t>(matchHistoryPage * matchHistoryRowsPerPage);
                    const std::size_t pageEnd = std::min(matchSummaries.size(),
                        pageStart + static_cast<std::size_t>(matchHistoryRowsPerPage));
                    bool openedReplay = false;
                    for (std::size_t i = pageStart; i < pageEnd; ++i) {
                        const float rowY = 252.f + static_cast<float>(i - pageStart) * 48.f;
                        if (mouseX >= 275.f && mouseX <= 925.f && mouseY >= rowY && mouseY <= rowY + 42.f) {
                            playSound(uiClickSoundBuffer);
                            replayMatch = matchSummaries[i];
                            replayMoveIndex = -1;
                            replayBoard = Board();
                            replayActive = true;
                            openedReplay = true;
                            break;
                        }
                    }
                    if (openedReplay) continue;

                    if (mouseX >= 500.f && mouseX <= 700.f &&
                        mouseY >= 665.f && mouseY <= 720.f) {
                        currentScreen = matchHistoryReturnScreen;
                    }
                    continue;
                }

                if (currentScreen == Screen::StartScreen) {
                    if (mouseX >= 440.f && mouseX <= 760.f && mouseY >= 250.f && mouseY <= 325.f) {
                        playSound(uiClickSoundBuffer);
                        currentScreen = Screen::ChooseSide;
                    }
                    continue;
                }

                if (currentScreen == Screen::ChooseSide) {
                    if (mouseX >= 440.f && mouseX <= 760.f && mouseY >= 300.f && mouseY <= 370.f) {
                        playSound(uiClickSoundBuffer);
                        startGame(1);
                    }

                    else if (mouseX >= 440.f && mouseX <= 760.f && mouseY >= 400.f && mouseY <= 470.f) {
                        playSound(uiClickSoundBuffer);
                        startGame(0);
                    }

                    continue;
                }

                if (currentScreen == Screen::Promotion) {
                    int chosenIndex = -1;
                    constexpr float optionX = 402.f;
                    constexpr float optionY = 358.f;
                    constexpr float optionSize = 84.f;
                    constexpr float optionStep = 104.f;

                    for (int i = 0; i < 4; ++i) {
                        const float x = optionX + i * optionStep;
                        if (mouseX >= x && mouseX < x + optionSize &&
                            mouseY >= optionY && mouseY < optionY + optionSize) {
                            chosenIndex = i;
                            break;
                        }
                    }

                    if (chosenIndex >= 0) {
                        playSound(uiClickSoundBuffer);
                        Move promotion = pendingPromotion;
                        promotion.special_move = promotionMoves[chosenIndex];
                        if (applyMove(promotion)) {
                            if (currentScreen == Screen::Promotion)
                                currentScreen = Screen::Game;
                        }
                    }

                    continue;
                }

                if (currentScreen == Screen::Settings) {
                    if (mouseX >= 28.f && mouseX <= 64.f &&
                        mouseY >= 30.f && mouseY <= 78.f) {
                        playSound(uiClickSoundBuffer);
                        currentScreen = previousScreen;
                    }

                    else if (mouseX >= 812.f && mouseX <= 952.f &&
                        mouseY >= 42.f && mouseY <= 80.f) {
                        playSound(uiClickSoundBuffer);
                        settingsTab = 0;
                    }

                    else if (mouseX >= 962.f && mouseX <= 1102.f &&
                             mouseY >= 42.f && mouseY <= 80.f) {
                        playSound(uiClickSoundBuffer);
                        settingsTab = 1;
                        refreshPieceThemes();
                        refreshBoardThemes();
                    }

                    else if (settingsTab == 0) {
                        auto clickedGameplayRow = [&](float y) {
                            return mouseX >= 140.f && mouseX <= 1060.f &&
                                   mouseY >= y && mouseY <= y + 48.f;
                        };

                        if (mouseY >= 214.f && mouseY <= 246.f) {
                            PlayerMoveMode selectedMode;
                            if (mouseX >= 650.f && mouseX <= 775.f)
                                selectedMode = PlayerMoveMode::Slide;
                            else if (mouseX >= 785.f && mouseX <= 910.f)
                                selectedMode = PlayerMoveMode::Drag;
                            else if (mouseX >= 920.f && mouseX <= 1045.f)
                                selectedMode = PlayerMoveMode::PointAndClick;
                            else
                                continue;

                            playSound(uiClickSoundBuffer);
                            playerMoveMode = selectedMode;
                            clearSelection();
                        }

                        else if (clickedGameplayRow(270.f)) {
                            playSound(uiClickSoundBuffer);
                            showPossibleMoves = !showPossibleMoves;

                            if (!showPossibleMoves)
                                legalMoves.clear();
                            else if (pieceSelected)
                                selectPiece(selectedRow, selectedCol);
                        }

                        else if (clickedGameplayRow(335.f)) {
                            playSound(uiClickSoundBuffer);
                            showCoordinates = !showCoordinates;
                        }

                        else if (clickedGameplayRow(400.f)) {
                            playSound(uiClickSoundBuffer);
                            soundEffects = !soundEffects;
                        }

                        else if (clickedGameplayRow(465.f)) {
                            playSound(uiClickSoundBuffer);
                            historyStyle = historyStyle == HistoryStyle::Pictogram
                                ? HistoryStyle::Algebraic
                                : HistoryStyle::Pictogram;
                        }

                        else if (clickedGameplayRow(530.f)) {
                            playSound(uiClickSoundBuffer);
                            toggleFullscreen();
                            continue;
                        }

                        else if (clickedGameplayRow(595.f)) {
                            playSound(uiClickSoundBuffer);
                            showEvaluationBar = !showEvaluationBar;
                        }

                    }

                    else {
                        for (int i = 0; i < static_cast<int>(pieceThemeFolders.size()); i++) {
                            const float x = 135.f + i * 250.f;
                            const float y = 235.f;

                            if (mouseX >= x && mouseX <= x + 230.f &&
                                mouseY >= y && mouseY <= y + 340.f) {
                                playSound(uiClickSoundBuffer);
                                pieceTheme = i;
                                boardTheme = i;
                                loadPieceTextures();
                                loadBoardTextures();
                                break;
                            }
                        }
                    }

                    if (gameStarted &&
                        mouseX >= 350.f && mouseX <= 500.f &&
                        mouseY >= 662.f && mouseY <= 704.f) {
                        playSound(uiClickSoundBuffer);
                        startGame(engineSide);
                        continue;
                    }

                    if (mouseX >= 525.f && mouseX <= 675.f &&
                        mouseY >= 662.f && mouseY <= 704.f) {
                        playSound(uiClickSoundBuffer);
                        currentScreen = Screen::ChooseSide;
                        hoveredHistoryNode = -1;
                        continue;
                    }

                    if (mouseX >= 700.f && mouseX <= 850.f &&
                        mouseY >= 662.f && mouseY <= 704.f) {
                        playSound(uiClickSoundBuffer);
                        currentScreen = previousScreen;
                        continue;
                    }

                    continue;
                }

                if (currentScreen == Screen::EndGame) {
                    if (mouseX >= 390.f && mouseX <= 590.f &&
                        mouseY >= 500.f && mouseY <= 565.f) {
                        playSound(uiClickSoundBuffer);
                        startGame(engineSide);
                        continue;
                    }

                    if (mouseX >= 610.f && mouseX <= 810.f &&
                        mouseY >= 500.f && mouseY <= 565.f) {
                        playSound(uiClickSoundBuffer);
                        previousScreen = Screen::EndGame;
                        currentScreen = Screen::Settings;
                        settingsTab = 0;
                        continue;
                    }

                    if (mouseX >= 500.f && mouseX <= 700.f &&
                        mouseY >= 590.f && mouseY <= 650.f) {
                        playSound(uiClickSoundBuffer);
                        currentScreen = Screen::ChooseSide;
                        continue;
                    }

                    continue;
                }

                if (currentScreen != Screen::Game || engineMoveAnimating)
                    continue;

                if (mouseX >= topFlipX && mouseX <= topFlipX + 74.f &&
                    mouseY >= 20.f && mouseY <= 56.f) {
                    playSound(uiClickSoundBuffer);
                    manualBoardFlip = !manualBoardFlip;
                    clearSelection();
                    continue;
                }

                if (mouseX >= topSettingsX && mouseX <= topSettingsX + 160.f &&
                    mouseY >= 20.f && mouseY <= 56.f) {
                    playSound(uiClickSoundBuffer);
                    previousScreen = Screen::Game;
                    settingsTab = 0;
                    currentScreen = Screen::Settings;
                    clearSelection();
                    continue;
                }

                if (mouseX >= sidePanelX + sidePanelWidth - 114.f &&
                    mouseX <= sidePanelX + sidePanelWidth - 10.f &&
                    mouseY >= sidePanelY + 55.f && mouseY <= sidePanelY + 73.f) {
                    historyPreviewHeld = true;
                    continue;
                }

                if (historyBrowsing &&
                    mouseX >= sidePanelX + sidePanelWidth - 228.f &&
                    mouseX <= sidePanelX + sidePanelWidth - 124.f &&
                    mouseY >= sidePanelY + 55.f && mouseY <= sidePanelY + 73.f) {
                    playSound(uiClickSoundBuffer);
                    viewingHistoryNode = currentHistoryNode;
                    forceNewBranch = engineTurn();
                    clockKeyNode = -2;
                    historyBrowsing = false;
                    invalidateSearch();
                    engineMoveWaiting = false;
                    clearSelection();
                    continue;
                }

                const HistoryScrollBar verticalBar = getHistoryScrollBar();

                const float iconSize = 45.f;
                std::vector<sf::Vector2f> historyPositions = getHistoryNodePositions();
                const float historyLeft = sidePanelX + 20.f;
                const float historyRight = verticalBar.x - 8.f;
                const float historyTop = verticalBar.y;
                const float historyBottom = verticalBar.y + verticalBar.height;

                bool clickedHistory = false;

                const sf::Vector2f historyPinnedPos{sidePanelX + 25.f, sidePanelY + 75.f};

                for (int i = 0; i < static_cast<int>(historyTree.size()); i++)
                {
                    const bool pinned = i == 0;
                    float nodeX = pinned ? historyPinnedPos.x : historyPositions[i].x;
                    float nodeY = pinned
                        ? historyPinnedPos.y
                        : historyPositions[i].y - historyScroll;

                    if ((pinned || (nodeX >= historyLeft && nodeX + iconSize <= historyRight &&
                        nodeY >= historyTop && nodeY + iconSize <= historyBottom)) &&
                        mouseX >= nodeX && mouseX <= nodeX + iconSize &&
                        mouseY >= nodeY && mouseY <= nodeY + iconSize)
                    {
                        playSound(uiClickSoundBuffer);
                        const int parentNode = historyTree[i].parent;
                        currentHistoryNode = parentNode;
                        viewingHistoryNode = i;
                        invalidateSearch();
                        game = parentNode == -1
                            ? startingBoard
                            : historyTree[parentNode].move.boardAfterMove;
                        rebuildLinearHistoryFromNode(currentHistoryNode);

                        historyBrowsing = true;
                        forceNewBranch = false;
                        engineMoveWaiting = false;
                        clearSelection();

                        clickedHistory = true;
                        break;
                    }
                }

                if (clickedHistory || engineTurn() || game.has_game_ended()) continue;

                if (mouseX < boardX || mouseX >= boardX + boardSize ||
                    mouseY < boardY || mouseY >= boardY + boardSize) continue;

                int shownCol = static_cast<int>((mouseX - boardX) / squareSize);
                int shownRow = static_cast<int>((mouseY - boardY) / squareSize);

                int col = isBoardFlipped() ? 7 - shownCol : shownCol;
                int row = isBoardFlipped() ? 7 - shownRow : shownRow;

                if (row < 0 || row >= 8 || col < 0 || col >= 8)
                    continue;

                if (playerMoveMode != PlayerMoveMode::Drag) {
                    if (!pieceSelected) {
                        auto boardRow = game.begin() + row;
                        int piece = (*boardRow)[col];

                        if (piece != 0 && (piece < 0) == (game.get_current_turn() == 1)) {
                            selectPiece(row, col);
                        }
                    }

                    else {
                        if (row == selectedRow && col == selectedCol) {
                            clearSelection();
                            continue;
                        }

                        auto boardRow = game.begin() + row;
                        int piece = (*boardRow)[col];

                        if (piece != 0 && (piece < 0) == (game.get_current_turn() == 1)) {
                            selectPiece(row, col);
                            continue;
                        }

                        if (game.valid_move(selectedRow, selectedCol, row, col)) {
                            if (playerMoveMode == PlayerMoveMode::Slide) {
                                animatedMove = Move{selectedRow, selectedCol, row, col};
                                animatedFromRow = selectedRow;
                                animatedFromCol = selectedCol;
                                animatedToRow = row;
                                animatedToCol = col;
                                animatedPiece =
                                    (*(game.begin() + selectedRow))[selectedCol];
                                engineMoveAnimating = true;
                                engineMoveWaiting = false;
                                engineAnimationClock.restart();
                                clearSelection();
                            }
                            else if (applyMove(Move{selectedRow, selectedCol, row, col})) {
                                clearSelection();
                            }
                        }
                    }

                    continue;
                }

                auto boardRow = game.begin() + row;
                int piece = (*boardRow)[col];

                if (piece != 0 && (piece < 0) == (game.get_current_turn() == 1)) {
                    dragging = true;
                    draggedRow = row;
                    draggedCol = col;
                    mousePosition = {mouseX, mouseY};
                    legalMoves.clear();

                    if (showPossibleMoves) {
                        for (int newRow = 0; newRow < 8; newRow++) {
                            for (int newCol = 0; newCol < 8; newCol++) {
                                if (game.valid_move(draggedRow, draggedCol, newRow, newCol)) {
                                    legalMoves.push_back({newRow, newCol});
                                }
                            }
                        }
                    }
                }
            }
        }

        if (const auto* mouseMoved = event->getIf<sf::Event::MouseMoved>()) {
            mousePosition = window.mapPixelToCoords(mouseMoved->position, logicalView);
        }

        if (const auto* mouseReleased = event->getIf<sf::Event::MouseButtonReleased>()) {
            if (mouseReleased->button == sf::Mouse::Button::Left) {
                if (historyHorizontalScrollbarDragging) {
                    const sf::Vector2f logicalMouse = window.mapPixelToCoords(mouseReleased->position, logicalView);
                    scrollHistoryHorizontalFromMouse(logicalMouse.x);
                }
                historyHorizontalScrollbarDragging = false;
                historyPreviewHeld = false;
                if (dragging) {
                    const sf::Vector2f logicalMouse = window.mapPixelToCoords(mouseReleased->position, logicalView);
                    float mouseX = logicalMouse.x;
                    float mouseY = logicalMouse.y;
                    int shownCol = static_cast<int>((mouseX - boardX) / squareSize);
                    int shownRow = static_cast<int>((mouseY - boardY) / squareSize);

                    int newCol = isBoardFlipped() ? 7 - shownCol : shownCol;
                    int newRow = isBoardFlipped() ? 7 - shownRow : shownRow;

                    if (!engineTurn() && !game.has_game_ended() &&
                        mouseX >= boardX && mouseX < boardX + boardSize &&
                        mouseY >= boardY && mouseY < boardY + boardSize) {
                        applyMove(Move{draggedRow, draggedCol, newRow, newCol});
                    }

                    dragging = false;
                    draggedRow = -1;
                    draggedCol = -1;
                    legalMoves.clear();
                }
            }
        }
    }
}

void ChessUI::draw() {
    window.setView(logicalView);
    window.clear(sf::Color(30, 30, 30));

    if (currentScreen == Screen::StartScreen) {
        drawStartScreen();
        window.display();
        return;
    }

    if (currentScreen == Screen::ChooseSide) {
        drawChooseSide();
        drawProfileButton();
        window.display();
        return;
    }

    if (currentScreen == Screen::Settings) {
        drawSettings();
        window.display();
        return;
    }

    if (currentScreen == Screen::Account) {
        drawAccountScreen();
        window.display();
        return;
    }

    if (currentScreen == Screen::MatchHistory) {
        drawMatchHistoryScreen();
        drawProfileButton();
        window.display();
        return;
    }

    if (currentScreen == Screen::EndGame) {
        drawBoard();
        drawLastMoveHighlights();

        if (showCoordinates)
            drawCoordinates();

        drawPieces();
        drawCapturedPieces();
        drawSidePanel();
        drawText();
        drawClocks();
        drawMoveHistory();
        if (showEvaluationBar)
            drawEvaluationBar();
        drawEndScreen();
        drawProfileButton();
        window.display();
        return;
    }

    drawBoard();
    drawLastMoveHighlights();

    if (showCoordinates)
        drawCoordinates();

    drawLegalMoves();
    drawPieces();

    if (playerMoveMode != PlayerMoveMode::Drag && pieceSelected) {
        sf::RectangleShape selectedSquare( sf::Vector2f(squareSize - 6.f, squareSize - 6.f) );
        selectedSquare.setPosition({
            static_cast<float>(boardX + displayCol(selectedCol) * squareSize + 3),
            static_cast<float>(boardY + displayRow(selectedRow) * squareSize + 3)
        });
        selectedSquare.setFillColor(sf::Color::Transparent);
        selectedSquare.setOutlineColor(sf::Color(255, 215, 0));
        selectedSquare.setOutlineThickness(4.f);
        window.draw(selectedSquare);
    }

    drawHistoryPreview();
    drawCapturedPieces();

    drawSidePanel();
    drawText();
    drawClocks();
    drawMoveHistory();
    if (showEvaluationBar)
        drawEvaluationBar();

    sf::RectangleShape flipButton({74.f, 36.f});
    flipButton.setPosition({topFlipX, 20.f});
    flipButton.setFillColor(sf::Color(60, 60, 60));
    window.draw(flipButton);

    sf::Text flipText(font, "FLIP", 13);
    flipText.setPosition({topFlipX + 22.f, 29.f});
    flipText.setFillColor(sf::Color::White);
    window.draw(flipText);

    sf::RectangleShape settingsButton({160.f, 36.f});
    settingsButton.setPosition({topSettingsX, 20.f});
    settingsButton.setFillColor(sf::Color(60, 60, 60));
    window.draw(settingsButton);

    sf::Text settingsText(font, "SETTINGS", 15);
    settingsText.setPosition({topSettingsX + 40.f, 28.f});
    settingsText.setFillColor(sf::Color::White);
    window.draw(settingsText);

    if (currentScreen == Screen::Promotion)
        drawPromotionDialog();
    else
        drawProfileButton();

    window.display();
}

void ChessUI::drawPromotionDialog() {
    sf::RectangleShape overlay({static_cast<float>(windowWidth), static_cast<float>(windowHeight)});
    overlay.setFillColor(sf::Color(0, 0, 0, 175));
    window.draw(overlay);

    sf::RectangleShape dialog({520.f, 180.f});
    dialog.setPosition({340.f, 310.f});
    dialog.setFillColor(sf::Color(38, 38, 38));
    dialog.setOutlineColor(sf::Color(220, 220, 220));
    dialog.setOutlineThickness(2.f);
    window.draw(dialog);

    int side = 1;
    if (pendingPromotion.old_x >= 0 && pendingPromotion.old_x < 8 && pendingPromotion.old_y >= 0 && pendingPromotion.old_y < 8) {
        const int pawn = (*(game.begin() + pendingPromotion.old_x))[pendingPromotion.old_y];
        side = pawn < 0 ? -1 : 1;
    }

    for (int i = 0; i < 4; ++i) {
        const float x = 402.f + static_cast<float>(i) * 104.f;
        const float y = 358.f;

        sf::RectangleShape option({84.f, 84.f});
        option.setPosition({x, y});
        const bool hovered = mousePosition.x >= x && mousePosition.x <= x + 84.f &&
            mousePosition.y >= y && mousePosition.y <= y + 84.f;
        option.setFillColor(hovered
            ? sf::Color(105, 91, 55) : sf::Color(70, 70, 70));
        option.setOutlineColor(sf::Color(210, 190, 120));
        option.setOutlineThickness(2.f);
        window.draw(option);

        sf::Texture& texture = pieceTextures[side * promotionPieces[i]];
        sf::Sprite piece(texture);
        const sf::Vector2u size = texture.getSize();
        const float maxSize = static_cast<float>(std::max(size.x, size.y));
        const float scale = 58.f / maxSize;
        piece.setScale({scale, scale});
        piece.setPosition({x + (84.f - size.x * scale) / 2.f,
            y + (84.f - size.y * scale) / 2.f});
        window.draw(piece);
    }
}

void ChessUI::drawCoverTexture(const sf::Texture& texture) {
    sf::Sprite background(texture);

    auto textureSize = texture.getSize();
    float scaleX = static_cast<float>(windowWidth) / textureSize.x;
    float scaleY = static_cast<float>(windowHeight) / textureSize.y;
    float scale = std::max(scaleX, scaleY);

    background.setScale({scale, scale});

    float width = textureSize.x * scale;
    float height = textureSize.y * scale;

    background.setPosition({
        (windowWidth - width) / 2.f,
        (windowHeight - height) / 2.f
    });

    window.draw(background);
}

void ChessUI::updateSelectionVideo() {
    if (selectionFrameClock.getElapsedTime().asSeconds() < 0.125f)
        return;

    selectionFrameClock.restart();

    selectionFrame++;

    if (selectionFrame > selectionFrameCount)
        selectionFrame = 1;

    std::ostringstream filename;

    filename << "assets/design/selection_frames/frame"
             << std::setfill('0') << std::setw(3)
             << selectionFrame << ".jpg";

    static_cast<void>(selectionFrameTexture.loadFromFile(filename.str()));
}

void ChessUI::drawStartScreen() {
    drawCoverTexture(startBackgroundTexture);

    sf::RectangleShape darkOverlay({1200.f, 800.f});
    darkOverlay.setFillColor(sf::Color(0, 0, 0, 55));
    window.draw(darkOverlay);

    sf::Text title(font, "CHESS", 64);
    title.setPosition({500.f, 105.f});
    title.setFillColor(sf::Color::White);
    window.draw(title);

    sf::RectangleShape startButton({320.f, 75.f});
    startButton.setPosition({440.f, 250.f});
    startButton.setFillColor(sf::Color(20, 20, 20, 205));
    startButton.setOutlineColor(sf::Color(220, 220, 220));
    startButton.setOutlineThickness(2.f);
    window.draw(startButton);

    sf::Text startText(font, "START", 28);
    startText.setPosition({550.f, 270.f});
    startText.setFillColor(sf::Color::White);
    window.draw(startText);
}

void ChessUI::drawProfileButton() {
    sf::CircleShape background(topProfileRadius);
    background.setPosition({topProfileX, 13.f});
    background.setTexture(&profilePictureTexture);
    background.setOutlineThickness(0.f);
    window.draw(background);

    if (accountSignedIn) {
        sf::CircleShape signedInDot(4.f);
        signedInDot.setPosition({topProfileX + 41.f, 52.f});
        signedInDot.setFillColor(sf::Color(90, 190, 105));
        signedInDot.setOutlineColor(sf::Color(18, 20, 22));
        signedInDot.setOutlineThickness(1.f);
        window.draw(signedInDot);
    }
}

void ChessUI::drawChooseSide() {
    updateSelectionVideo();
    drawCoverTexture(selectionFrameTexture);

    sf::RectangleShape darkOverlay({1200.f, 800.f});
    darkOverlay.setFillColor(sf::Color(0, 0, 0, 95));
    window.draw(darkOverlay);

    sf::Text title(font, "CHOOSE YOUR SIDE", 42);
    title.setPosition({390.f, 140.f});
    title.setFillColor(sf::Color::White);
    window.draw(title);

    const float buttonWidth = 320.f;
    const float buttonHeight = 70.f;
    const float buttonX = 440.f;

    sf::RectangleShape whiteButton({buttonWidth, buttonHeight});
    whiteButton.setPosition({buttonX, 300.f});
    whiteButton.setFillColor(sf::Color(20, 20, 20, 210));
    whiteButton.setOutlineColor(sf::Color(220, 220, 220));
    whiteButton.setOutlineThickness(2.f);
    window.draw(whiteButton);

    sf::Text whiteText(font, "PLAY AS WHITE", 24);
    whiteText.setPosition({490.f, 320.f});
    whiteText.setFillColor(sf::Color::White);
    window.draw(whiteText);

    sf::RectangleShape blackButton({buttonWidth, buttonHeight});
    blackButton.setPosition({buttonX, 400.f});
    blackButton.setFillColor(sf::Color(20, 20, 20, 210));
    blackButton.setOutlineColor(sf::Color(220, 220, 220));
    blackButton.setOutlineThickness(2.f);
    window.draw(blackButton);

    sf::Text blackText(font, "PLAY AS BLACK", 24);
    blackText.setPosition({490.f, 420.f});
    blackText.setFillColor(sf::Color::White);
    window.draw(blackText);
}

void ChessUI::drawAccountScreen() {
    if (accountSignedIn) {
        sf::RectangleShape shade({1200.f, 800.f});
        shade.setFillColor(sf::Color(10, 11, 13, 235));
        window.draw(shade);

        sf::RectangleShape panel({600.f, 640.f});
        panel.setPosition({300.f, 80.f});
        panel.setFillColor(sf::Color(28, 30, 33));
        panel.setOutlineColor(sf::Color(70, 73, 78));
        panel.setOutlineThickness(1.f);
        window.draw(panel);

        sf::Text title(font, "PROFILE", 34);
        title.setPosition({510.f, 108.f});
        title.setFillColor(sf::Color::White);
        window.draw(title);

        sf::CircleShape avatar(44.f);
        avatar.setPosition({556.f, 162.f});
        avatar.setTexture(&profilePictureTexture);
        avatar.setOutlineColor(sf::Color(110, 114, 121));
        avatar.setOutlineThickness(1.f);
        window.draw(avatar);

        sf::Text signedInAs(font, "SIGNED IN AS", 14);
        signedInAs.setPosition({370.f, 268.f});
        signedInAs.setFillColor(sf::Color(160, 163, 169));
        window.draw(signedInAs);

        sf::Text profileName(font, accountName, 22);
        profileName.setPosition({370.f, 287.f});
        profileName.setFillColor(sf::Color::White);
        window.draw(profileName);

        if (!accountMessage.empty()) {
            sf::Text statusText(font, accountMessage, 13);
            statusText.setPosition({370.f, 393.f});
            statusText.setFillColor(sf::Color(245, 170, 135));
            window.draw(statusText);
        }

        sf::RectangleShape historyButton({340.f, 52.f});
        historyButton.setPosition({430.f, 430.f});
        historyButton.setFillColor(sf::Color(45, 48, 52));
        historyButton.setOutlineColor(sf::Color(74, 78, 84));
        historyButton.setOutlineThickness(1.f);
        window.draw(historyButton);

        sf::Text historyText(font, "MATCH HISTORY", 17);
        historyText.setPosition({531.f, 446.f});
        historyText.setFillColor(sf::Color::White);
        window.draw(historyText);

        sf::RectangleShape signOutButton({340.f, 52.f});
        signOutButton.setPosition({430.f, 495.f});
        signOutButton.setFillColor(sf::Color(94, 52, 49));
        signOutButton.setOutlineColor(sf::Color(128, 73, 68));
        signOutButton.setOutlineThickness(1.f);
        window.draw(signOutButton);

        sf::Text signOutText(font, "SIGN OUT", 17);
        signOutText.setPosition({558.f, 511.f});
        signOutText.setFillColor(sf::Color::White);
        window.draw(signOutText);

        sf::RectangleShape backButton({170.f, 44.f});
        backButton.setPosition({515.f, 585.f});
        backButton.setFillColor(sf::Color(39, 41, 45));
        backButton.setOutlineColor(sf::Color(65, 68, 73));
        backButton.setOutlineThickness(1.f);
        window.draw(backButton);

        sf::Text backText(font, "BACK", 16);
        backText.setPosition({574.f, 598.f});
        backText.setFillColor(sf::Color::White);
        window.draw(backText);
        if (profilePicturePickerOpen)
            drawProfilePicturePicker();
        return;
    }

    sf::RectangleShape shade({1200.f, 800.f});
    shade.setFillColor(sf::Color(20, 20, 20, 210));
    window.draw(shade);

    sf::RectangleShape panel({600.f, 580.f});
    panel.setPosition({300.f, 120.f});
    panel.setFillColor(sf::Color(42, 42, 42));
    panel.setOutlineColor(sf::Color(190, 190, 190));
    panel.setOutlineThickness(2.f);
    window.draw(panel);

    sf::Text title(font, accountSignUpMode ? "CREATE ACCOUNT" : "SIGN IN", 34);
    title.setPosition({420.f, 153.f});
    title.setFillColor(sf::Color::White);
    window.draw(title);

    sf::Text usernameLabel(font, "Username", 18);
    usernameLabel.setPosition({370.f, 225.f});
    usernameLabel.setFillColor(sf::Color(225, 225, 225));
    window.draw(usernameLabel);

    sf::RectangleShape usernameBox({460.f, 54.f});
    usernameBox.setPosition({370.f, 250.f});
    usernameBox.setFillColor(sf::Color(27, 27, 27));
    usernameBox.setOutlineColor(accountPasswordField
        ? sf::Color(95, 95, 95) : sf::Color(230, 190, 90));
    usernameBox.setOutlineThickness(2.f);
    window.draw(usernameBox);

    sf::Text username(font,
        accountInput.empty() ? "Enter username" : accountInput, 18);
    username.setPosition({388.f, 266.f});
    username.setFillColor(accountInput.empty()
        ? sf::Color(145, 145, 145) : sf::Color::White);
    window.draw(username);

    sf::Text passwordLabel(font, "Password", 18);
    passwordLabel.setPosition({370.f, 324.f});
    passwordLabel.setFillColor(sf::Color(225, 225, 225));
    window.draw(passwordLabel);

    sf::RectangleShape passwordBox({460.f, 54.f});
    passwordBox.setPosition({370.f, 349.f});
    passwordBox.setFillColor(sf::Color(27, 27, 27));
    passwordBox.setOutlineColor(accountPasswordField
        ? sf::Color(230, 190, 90) : sf::Color(95, 95, 95));
    passwordBox.setOutlineThickness(2.f);
    window.draw(passwordBox);

    const std::string maskedPassword = accountPassword.empty()
        ? "Enter password" : std::string(accountPassword.size(), '*');
    sf::Text password(font, maskedPassword, 18);
    password.setPosition({388.f, 365.f});
    password.setFillColor(accountPassword.empty()
        ? sf::Color(145, 145, 145) : sf::Color::White);
    window.draw(password);

    sf::RectangleShape modeButton({460.f, 48.f});
    modeButton.setPosition({370.f, 425.f});
    modeButton.setFillColor(sf::Color(58, 58, 58));
    window.draw(modeButton);

    sf::Text modeText(font, accountSignUpMode
        ? "Already registered? Sign in" : "New here? Create an account", 17);
    modeText.setPosition({435.f, 439.f});
    modeText.setFillColor(sf::Color::White);
    window.draw(modeText);

    const std::string status = accountMessage;
    sf::Text statusText(font, status, 14);
    statusText.setPosition({370.f, 487.f});
    statusText.setFillColor(accountMessage.empty()
        ? sf::Color(190, 190, 190) : sf::Color(245, 190, 120));
    window.draw(statusText);

    sf::RectangleShape submitButton({340.f, 56.f});
    submitButton.setPosition({430.f, 530.f});
    submitButton.setFillColor(sf::Color(70, 125, 75));
    window.draw(submitButton);

    sf::Text submitText(font, accountSignUpMode ? "SIGN UP" : "LOG IN", 20);
    submitText.setPosition({552.f, 547.f});
    submitText.setFillColor(sf::Color::White);
    window.draw(submitText);

    sf::RectangleShape backButton({200.f, 50.f});
    backButton.setPosition({500.f, 620.f});
    backButton.setFillColor(sf::Color(62, 62, 62));
    window.draw(backButton);

    sf::Text backText(font, "BACK", 18);
    backText.setPosition({570.f, 634.f});
    backText.setFillColor(sf::Color::White);
    window.draw(backText);
}

void ChessUI::drawProfilePicturePicker() {
    sf::RectangleShape shade({1200.f, 800.f});
    shade.setFillColor(sf::Color(0, 0, 0, 185));
    window.draw(shade);

    sf::RectangleShape panel({800.f, 590.f});
    panel.setPosition({200.f, 120.f});
    panel.setFillColor(sf::Color(28, 30, 33));
    panel.setOutlineColor(sf::Color(90, 94, 100));
    panel.setOutlineThickness(2.f);
    window.draw(panel);

    sf::Text title(font, "CHOOSE A PROFILE PICTURE", 27);
    title.setPosition({390.f, 145.f});
    title.setFillColor(sf::Color::White);
    window.draw(title);

    sf::RectangleShape closeButton({42.f, 38.f});
    closeButton.setPosition({930.f, 133.f});
    closeButton.setFillColor(sf::Color(67, 48, 48));
    window.draw(closeButton);
    sf::Text closeText(font, "X", 18);
    closeText.setPosition({944.f, 141.f});
    closeText.setFillColor(sf::Color::White);
    window.draw(closeText);

    constexpr float cardWidth = 90.f;
    constexpr float cardHeight = 82.f;
    constexpr float cardGapX = 18.f;
    constexpr float cardStartX = 285.f;
    constexpr float cardStartY = 245.f;
    constexpr int cardColumns = 6;

    for (std::size_t i = 0; i < profilePictureOptions.size(); ++i) {
        const int row = static_cast<int>(i) / cardColumns;
        const int col = static_cast<int>(i) % cardColumns;
        const float x = cardStartX + col * (cardWidth + cardGapX);
        const float y = cardStartY + row * 92.f - profilePicturePickerScroll;
        if (y < cardStartY || y + cardHeight > 645.f)
            continue;

        const bool selected = profilePictureOptions[i] == profilePicturePath;
        sf::RectangleShape choice({cardWidth, cardHeight});
        choice.setPosition({x, y});
        choice.setFillColor(selected
            ? sf::Color(82, 115, 82) : sf::Color(58, 58, 58));
        if (selected) {
            choice.setOutlineColor(sf::Color::White);
            choice.setOutlineThickness(2.f);
        }
        window.draw(choice);

        sf::CircleShape picture(30.f);
        picture.setPosition({x + (cardWidth - 60.f) / 2.f,
            y + (cardHeight - 60.f) / 2.f});
        picture.setFillColor(sf::Color::White);
        picture.setTexture(&profilePictureOptionTextures[i]);
        window.draw(picture);
    }

    const float contentHeight =
        static_cast<float>((profilePictureOptions.size() + 5) / 6) * 92.f;
    if (contentHeight > 400.f) {
        sf::Text scrollHint(font, "Scroll to see more pictures", 13);
        scrollHint.setPosition({488.f, 672.f});
        scrollHint.setFillColor(sf::Color(165, 168, 173));
        window.draw(scrollHint);
    }
}

void ChessUI::drawMatchHistoryScreen() {
    sf::RectangleShape shade({1200.f, 800.f});
    shade.setFillColor(sf::Color(24, 24, 24));
    window.draw(shade);

    if (replayActive) {
        sf::Text replayTitle(font, "MATCH REPLAY", 32);
        replayTitle.setPosition({static_cast<float>(sidePanelX), static_cast<float>(boardY)});
        replayTitle.setFillColor(sf::Color::White);
        window.draw(replayTitle);

        const std::string details[] = {
            "White:  " + replayMatch.white,
            "Black:  " + replayMatch.black,
            "Result:  " + replayMatch.result,
            "Date:  " + (replayMatch.date.empty() ? std::string("-") : replayMatch.date)
        };
        for (int line = 0; line < 4; ++line) {
            sf::Text detail(font, details[line], 18);
            detail.setPosition({static_cast<float>(sidePanelX), boardY + 60.f + line * 32.f});
            detail.setFillColor(sf::Color(200, 200, 200));
            window.draw(detail);
        }

        drawBoardPosition(replayBoard);

        if (replayMoveIndex >= 0) {
            const Move& lastMove = replayMatch.mainline[static_cast<std::size_t>(replayMoveIndex)];
            MoveHistoryEntry arrowEntry{};
            arrowEntry.fromRow = lastMove.old_x;
            arrowEntry.fromCol = lastMove.old_y;
            arrowEntry.toRow = lastMove.new_x;
            arrowEntry.toCol = lastMove.new_y;
            drawMoveArrow(arrowEntry);
        }

        drawReplayControls();
        return;
    }

    sf::Text title(font, "MATCH HISTORY", 36);
    title.setPosition({440.f, 96.f});
    title.setFillColor(sf::Color::White);
    window.draw(title);

    sf::Text count(font,
        "Matches: " + std::to_string(matchSummaries.size()), 17);
    count.setPosition({285.f, 165.f});
    count.setFillColor(sf::Color(210, 210, 210));
    window.draw(count);

    auto shortText = [](const std::string& value, std::size_t maxLength) {
        if (value.size() <= maxLength) return value;
        return value.substr(0, maxLength > 3 ? maxLength - 3 : 0) + "...";
    };

    if (matchSummaries.empty()) {
        const std::string emptyMessage = !matchHistoryMessage.empty()
            ? matchHistoryMessage
            : (matchHistoryProvider
                ? "No matches have been recorded yet."
                : "Match history is not connected yet.");
        sf::Text empty(font, emptyMessage, 22);
        empty.setPosition({365.f, 340.f});
        empty.setFillColor(sf::Color(200, 200, 200));
        window.draw(empty);
    }
    else {
        const float tableX = 275.f;
        const float tableY = 205.f;
        sf::RectangleShape header({650.f, 42.f});
        header.setPosition({tableX, tableY});
        header.setFillColor(sf::Color(67, 67, 67));
        window.draw(header);

        const char* headings[] = {"DATE", "WHITE", "BLACK", "RESULT"};
        const float columns[] = {292.f, 415.f, 565.f, 715.f};
        for (int i = 0; i < 4; ++i) {
            sf::Text heading(font, headings[i], 15);
            heading.setPosition({columns[i], tableY + 13.f});
            heading.setFillColor(sf::Color(230, 230, 230));
            window.draw(heading);
        }

        const std::size_t pageStart =
            static_cast<std::size_t>(matchHistoryPage * matchHistoryRowsPerPage);
        const std::size_t pageEnd = std::min(matchSummaries.size(),
            pageStart + static_cast<std::size_t>(matchHistoryRowsPerPage));

        for (std::size_t i = pageStart; i < pageEnd; ++i) {
            const float y = tableY + 47.f + static_cast<float>(i - pageStart) * 48.f;

            sf::RectangleShape row({650.f, 42.f});
            row.setPosition({tableX, y});
            row.setFillColor((i - pageStart) % 2 == 0
                ? sf::Color(48, 48, 48) : sf::Color(39, 39, 39));
            window.draw(row);

            const MatchSummary& match = matchSummaries[i];
            const std::string values[] = {
                shortText(match.date.empty() ? "-" : match.date, 12),
                shortText(match.white, 18),
                shortText(match.black, 18),
                shortText(match.result, 14)
            };
            for (int col = 0; col < 4; ++col) {
                sf::Text value(font, values[col], 15);
                value.setPosition({columns[col], y + 13.f});
                value.setFillColor(sf::Color::White);
                window.draw(value);
            }
        }

        drawMatchHistoryPagination();
    }

    sf::RectangleShape backButton({200.f, 55.f});
    backButton.setPosition({500.f, 665.f});
    backButton.setFillColor(sf::Color(65, 65, 65));
    window.draw(backButton);

    sf::Text backText(font, "BACK", 18);
    backText.setPosition({570.f, 682.f});
    backText.setFillColor(sf::Color::White);
    window.draw(backText);
}

void ChessUI::drawSmallButton(float x, float y, const std::string& label, bool enabled, sf::Color fill) {
    sf::RectangleShape button({90.f, 34.f});
    button.setPosition({x, y});
    button.setFillColor(enabled ? fill : sf::Color(42, 42, 42));
    window.draw(button);

    sf::Text text(font, label, 14);
    const sf::FloatRect bounds = text.getLocalBounds();
    text.setPosition({
        x + (90.f - bounds.size.x) / 2.f - bounds.position.x,
        y + (34.f - bounds.size.y) / 2.f - bounds.position.y
    });
    text.setFillColor(enabled ? sf::Color::White : sf::Color(110, 110, 110));
    window.draw(text);
}

void ChessUI::drawMatchHistoryPagination() {
    const int pages = std::max(1, static_cast<int>(
        (matchSummaries.size() + matchHistoryRowsPerPage - 1) / matchHistoryRowsPerPage));

    drawSmallButton(275.f, 602.f, "< PREV", matchHistoryPage > 0);

    sf::Text pageText(font,
        "Page " + std::to_string(matchHistoryPage + 1) + " / " + std::to_string(pages), 14);
    pageText.setPosition({385.f, 611.f});
    pageText.setFillColor(sf::Color(200, 200, 200));
    window.draw(pageText);

    drawSmallButton(555.f, 602.f, "NEXT >", matchHistoryPage < pages - 1);
}

void ChessUI::drawReplayControls() {
    const int total = static_cast<int>(replayMatch.mainline.size());

    drawSmallButton(275.f, 730.f, "< PREV", replayMoveIndex > -1);

    const std::string moveLabel = replayMoveIndex < 0
        ? "Start  (" + std::to_string(total) + " moves)"
        : "Move " + std::to_string(replayMoveIndex + 1) + " / " + std::to_string(total);
    sf::Text moveText(font, moveLabel, 15);
    moveText.setPosition({375.f, 739.f});
    moveText.setFillColor(sf::Color(210, 210, 210));
    window.draw(moveText);

    drawSmallButton(555.f, 730.f, "NEXT >", replayMoveIndex < total - 1);
    drawSmallButton(665.f, 730.f, "CLOSE", true, sf::Color(110, 60, 60));
}

void ChessUI::stepReplay(int delta) {
    const int target = std::clamp(replayMoveIndex + delta, -1,
        static_cast<int>(replayMatch.mainline.size()) - 1);
    if (target == replayMoveIndex)
        return;

    replayMoveIndex = target;
    replayBoard = Board();
    for (int i = 0; i <= replayMoveIndex; i++)
        replayBoard.make_move(replayMatch.mainline[static_cast<std::size_t>(i)]);
}

void ChessUI::updateEvaluationBar() {
    const int rawScore = whiteEngine.evaluate(game);
    const int whiteScore = game.get_current_turn() == 0 ? rawScore : -rawScore;
    const double pawns = std::clamp(whiteScore / 100.0, -15.0, 15.0);

    if (!evalStarted) {
        evalStarted = true;
        evalFrom = evalTo = evalShown = pawns;
        evalClock.restart();
        return;
    }

    if (pawns != evalTo) {
        evalFrom = evalShown;
        evalTo = pawns;
        evalClock.restart();
    }

    const float t = std::clamp(evalClock.getElapsedTime().asSeconds() / evalSlideSeconds, 0.f, 1.f);
    const float eased = 1.f - (1.f - t) * (1.f - t);
    evalShown = evalFrom + (evalTo - evalFrom) * eased;
}

void ChessUI::drawEvaluationBar() {
    const float width = 38.f;
    const float height = static_cast<float>(boardSize);
    const float x = static_cast<float>(boardX) - 48.f;
    const float y = static_cast<float>(boardY);
    const double whiteShare = 1.0 / (1.0 + std::exp(-evalShown / 3.0));

    sf::RectangleShape track({width, height});
    track.setPosition({x, y});
    track.setFillColor(sf::Color(35, 35, 35));
    track.setOutlineColor(sf::Color(100, 100, 100));
    track.setOutlineThickness(1.f);
    window.draw(track);

    const float whiteHeight = static_cast<float>(whiteShare) * height;
    sf::RectangleShape whiteFill({width, whiteHeight});
    whiteFill.setPosition({x, y + height - whiteHeight});
    whiteFill.setFillColor(sf::Color(235, 235, 235));
    window.draw(whiteFill);

    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%+.1f", evalShown);
    const bool whiteAhead = whiteShare >= 0.5;

    sf::Text text(font, buffer, 20);
    text.setFillColor(whiteAhead ? sf::Color(30, 30, 30) : sf::Color(230, 230, 230));
    const sf::FloatRect bounds = text.getLocalBounds();
    const float textY = whiteAhead ? y + height - bounds.size.y - 8.f : y + 4.f;
    text.setPosition({x + (width - bounds.size.x) / 2.f - bounds.position.x, textY - bounds.position.y});
    window.draw(text);
}

void ChessUI::openMatchHistory() {
    if (!accountSignedIn) return;
    recordCompletedMatch();
    matchHistoryReturnScreen = Screen::Account;
    matchHistoryPage = 0;
    replayActive = false;
    if (matchHistoryProvider) {
        try {
            matchSummaries = matchHistoryProvider(accountName);
            matchHistoryMessage.clear();
        } catch (...) {
            matchSummaries.clear();
            matchHistoryMessage = "Could not load local match history.";
        }
    }
    currentScreen = Screen::MatchHistory;
}

void ChessUI::recordCompletedMatch() {
    const bool lostOnTime = timeExpired && timeoutLoser >= 0;
    if ((!game.has_game_ended() && !lostOnTime) || gameRecorded || gameAccountName.empty() ||
        gamePlayerSide < 0 || !matchRecorder) return;

    MatchSummary summary;
    summary.white = gamePlayerSide == 0 ? gameAccountName : "Engine";
    summary.black = gamePlayerSide == 1 ? gameAccountName : "Engine";
    if (lostOnTime)
        summary.result = timeoutLoser == 0 ? "0-1 (time)" : "1-0 (time)";
    else
        summary.result = game.get_game_status() > 0 ? "1-0" : "0-1";
    const auto now = std::time(nullptr);
    if (const auto* date = std::localtime(&now)) {
        std::ostringstream formatted;
        formatted << std::put_time(date, "%Y-%m-%d");
        summary.date = formatted.str();
    }
    std::vector<Move> mainline;
    mainline.reserve(moveHistory.size());
    for (const auto& entry : moveHistory)
        mainline.push_back(entry.boardAfterMove.get_last_move());
    try {
        matchRecorder(gameAccountName, gamePlayerSide, summary, mainline);
        gameRecorded = true;
        matchSaveMessage.clear();
    } catch (...) {
        matchSaveMessage = "Could not save match history.";
    }
}

void ChessUI::submitAccountForm() {
    if (accountInput.empty() || accountPassword.empty()) {
        accountMessage = "Enter both a username and password.";
        return;
    }

    if (!authHandler) {
        accountMessage = "Local accounts are not connected.";
        return;
    }

    bool accepted = false;
    try {
        accepted = authHandler(accountInput, accountPassword, accountSignUpMode);
    }
    catch (...) {
        accountMessage = "Could not access local accounts.";
        return;
    }

    if (!accepted) {
        accountMessage = accountSignUpMode
            ? "Sign up failed. Check the details and try again."
            : "Sign in failed. Check the username and password.";
        return;
    }

    accountSignedIn = true;
    accountName = accountInput;
    if (gameStarted && !clockStarted && !gameRecorded)
        gameAccountName = accountName;
    accountPassword.clear();
    accountMessage.clear();
    profilePicturePickerOpen = false;
    currentScreen = Screen::Account;
}

void ChessUI::loadProfilePictureOptions() {
    profilePictureOptions.clear();
    profilePictureOptionTextures.clear();

    std::vector<std::filesystem::path> pictureFiles;
    for (const auto& entry : std::filesystem::directory_iterator("assets/pfp")) {
        if (entry.is_regular_file()) pictureFiles.push_back(entry.path());
    }
    std::sort(pictureFiles.begin(), pictureFiles.end());

    for (const std::filesystem::path& file : pictureFiles) {
        sf::Texture texture;
        static_cast<void>(texture.loadFromFile(file.string()));
        profilePictureOptions.push_back(file.string());
        profilePictureOptionTextures.push_back(std::move(texture));
    }
}

bool ChessUI::isBoardFlipped() const {
    return (engineSide == 1) != manualBoardFlip;
}

int ChessUI::displayRow(int row) const { return isBoardFlipped() ? 7 - row : row; }
int ChessUI::displayCol(int col) const { return isBoardFlipped() ? 7 - col : col; }

void ChessUI::drawBoard() {
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            float x = static_cast<float>(boardX + col * squareSize);
            float y = static_cast<float>(boardY + row * squareSize);

            sf::Texture& tileTexture =
                ((row + col) % 2 == 0)
                ? whiteTileTexture
                : blackTileTexture;

            sf::Sprite tile(tileTexture);
            const sf::Vector2u size = tileTexture.getSize();
            tile.setScale({
                static_cast<float>(squareSize) / size.x,
                static_cast<float>(squareSize) / size.y
            });
            tile.setPosition({x, y});
            window.draw(tile);
        }
    }
}

void ChessUI::drawLastMoveHighlights() {
    if (currentHistoryNode < 0 || currentHistoryNode >= static_cast<int>(historyTree.size()))
        return;

    auto drawSquare = [this](int row, int col, sf::Color color) {
        if (row < 0 || row >= 8 || col < 0 || col >= 8)
            return;

        sf::RectangleShape highlight({ static_cast<float>(squareSize), static_cast<float>(squareSize)});
        highlight.setPosition({ static_cast<float>(boardX + displayCol(col) * squareSize), static_cast<float>(boardY + displayRow(row) * squareSize)});
        highlight.setFillColor(color);
        window.draw(highlight);
    };

    const MoveHistoryEntry& last = historyTree[currentHistoryNode].move;
    const sf::Color sourceYellow(255, 220, 0, 105);
    const sf::Color destinationOrange(255, 155, 0, 120);
    const sf::Color finalDarkOrange(205, 112, 18, 130);

    const int parent = historyTree[currentHistoryNode].parent;
    if (parent >= 0 && parent < static_cast<int>(historyTree.size())) {
        const MoveHistoryEntry& previous = historyTree[parent].move;
        const bool sameSide = (previous.piece > 0) == (last.piece > 0);

        if (sameSide &&
            previous.toRow == last.fromRow && previous.toCol == last.fromCol) {
            drawSquare(previous.fromRow, previous.fromCol, sourceYellow);
            drawSquare(previous.toRow, previous.toCol, destinationOrange);
            drawSquare(last.toRow, last.toCol, finalDarkOrange);
            return;
        }

        if (sameSide) {
            drawSquare(previous.fromRow, previous.fromCol, sourceYellow);
            drawSquare(previous.toRow, previous.toCol, destinationOrange);
            drawSquare(last.fromRow, last.fromCol, sourceYellow);
            drawSquare(last.toRow, last.toCol, destinationOrange);
            return;
        }
    }

    drawSquare(last.fromRow, last.fromCol, sourceYellow);
    drawSquare(last.toRow, last.toCol, destinationOrange);
}

void ChessUI::drawSidePanel() {
    sf::RectangleShape panel( sf::Vector2f( sidePanelWidth, sidePanelHeight ) );
    panel.setPosition( sf::Vector2f( sidePanelX, sidePanelY ) );
    panel.setFillColor( sf::Color(45, 45, 45) );

    window.draw(panel);
}

void ChessUI::drawText() {
    std::string turnText = game.get_current_turn() == 0 ? "White" : "Black";
    std::string status = game.has_game_ended() ? "Game ended" :
        turnText + " to move (" + std::to_string(game.get_moves_left()) + " actions left)";
    if (engineStalled) status = engineSearchFailed ? "Engine search failed" : "Engine has no legal move";
    sf::Text turnLabel(font, status, 20);
    turnLabel.setPosition({static_cast<float>(boardX), 8.f});
    window.draw(turnLabel);

    sf::Text historyTitle(font, "MOVE HISTORY", 28);
    historyTitle.setPosition({sidePanelX + 20.f, sidePanelY + 20.f});
    historyTitle.setFillColor(sf::Color::White);
    window.draw(historyTitle);

    if (currentScreen == Screen::Game) {
        const float buttonY = sidePanelY + 55.f;
        const float buttonHeight = 18.f;

        sf::RectangleShape previewButton({104.f, buttonHeight});
        previewButton.setPosition({sidePanelX + sidePanelWidth - 114.f, buttonY});
        previewButton.setFillColor(historyPreviewHeld
            ? sf::Color(63, 128, 78) : sf::Color(60, 60, 60));
        previewButton.setOutlineColor(sf::Color(104, 108, 114));
        previewButton.setOutlineThickness(1.f);
        window.draw(previewButton);

        sf::Text previewLabel(font, "HOLD:PREVIEW", 10);
        previewLabel.setPosition({sidePanelX + sidePanelWidth - 108.f, buttonY + 3.f});
        previewLabel.setFillColor(sf::Color::White);
        window.draw(previewLabel);

        if (historyBrowsing) {
            sf::RectangleShape resumeButton({104.f, buttonHeight});
            resumeButton.setPosition({sidePanelX + sidePanelWidth - 228.f, buttonY});
            resumeButton.setFillColor(sf::Color(63, 128, 78));
            resumeButton.setOutlineColor(sf::Color(91, 157, 104));
            resumeButton.setOutlineThickness(1.f);
            window.draw(resumeButton);

            sf::Text resumeLabel(font, "RESUME", 11);
            resumeLabel.setPosition({sidePanelX + sidePanelWidth - 206.f, buttonY + 3.f});
            resumeLabel.setFillColor(sf::Color::White);
            window.draw(resumeLabel);
        }
    }
}

void ChessUI::drawPieces() {
    int draggedPiece = 0;
    int row = 0;

    for (auto& boardRow : game) {
        int col = 0;
        for (auto& piece : boardRow) {
            if (piece != 0) {
                if (engineMoveAnimating &&
                    row == animatedFromRow && col == animatedFromCol) {
                    col++;
                    continue;
                }

                if (dragging && row == draggedRow && col == draggedCol) {
                    draggedPiece = piece;
                    col++;
                    continue;
                }
                sf::Sprite sprite(pieceTextures[piece]);

                auto textureSize = pieceTextures[piece].getSize();
                float maxSize = static_cast<float>( std::max(textureSize.x, textureSize.y) );
                float scale = 64.f / maxSize;
                sprite.setScale({scale, scale});

                float width = textureSize.x * scale;
                float height = textureSize.y * scale;
                int shownCol = displayCol(col);
                int shownRow = displayRow(row);

                float x = boardX + shownCol * squareSize + (squareSize - width) / 2.f;
                float y = boardY + shownRow * squareSize + (squareSize - height) / 2.f;
                sprite.setPosition({x, y});
                window.draw(sprite);
            }
            col++;
        }
        row++;
    }

    if (dragging && draggedPiece != 0) {
        sf::Sprite sprite(pieceTextures[draggedPiece]);

        auto textureSize =
            pieceTextures[draggedPiece].getSize();

        float maxSize = static_cast<float>(
            std::max(textureSize.x, textureSize.y)
        );

        float scale = 120.f / maxSize;

        sprite.setScale({scale, scale});

        float width = textureSize.x * scale;
        float height = textureSize.y * scale;

        sprite.setPosition({
            mousePosition.x - width / 2.f,
            mousePosition.y - height / 2.f
        });

        window.draw(sprite);
    }

    if (engineMoveAnimating && animatedPiece != 0) {
        sf::Texture& texture = pieceTextures[animatedPiece];
        const sf::Vector2u textureSize = texture.getSize();
        const float maxSize = static_cast<float>(
            std::max(textureSize.x, textureSize.y));
        sf::Sprite sprite(texture);
        const float scale = 64.f / maxSize;
        const float width = textureSize.x * scale;
        const float height = textureSize.y * scale;
        const float linearProgress = std::clamp(
            engineAnimationClock.getElapsedTime().asSeconds() /
                engineSlideSeconds,
            0.f, 1.f);
        const float progress = 1.f - (1.f - linearProgress) * (1.f - linearProgress);

        const sf::Vector2f from{
            boardX + displayCol(animatedFromCol) * squareSize + squareSize / 2.f,
            boardY + displayRow(animatedFromRow) * squareSize + squareSize / 2.f};
        const sf::Vector2f to{
            boardX + displayCol(animatedToCol) * squareSize + squareSize / 2.f,
            boardY + displayRow(animatedToRow) * squareSize + squareSize / 2.f};
        sprite.setScale({scale, scale});
        sprite.setPosition({
            from.x + (to.x - from.x) * progress - width / 2.f,
            from.y + (to.y - from.y) * progress - height / 2.f});
        window.draw(sprite);
    }
}

void ChessUI::refreshPieceThemes() {
    const std::vector<std::pair<std::string, std::string>> pairs = {
        {"assets/pieces/",  "assets/board8/"},
        {"assets/pieces5/", "assets/board6/"},
        {"assets/pieces7/", "assets/board17/"},
        {"assets/pieces8/", "assets/board18/"}
    };

    pieceThemeFolders.clear();
    boardThemeFolders.clear();
    for (const auto& pair : pairs) {
        if (std::filesystem::exists(pair.first + "white_king.png") &&
            std::filesystem::exists(pair.second + "white_tile.png") &&
            std::filesystem::exists(pair.second + "black_tile.png")) {
            pieceThemeFolders.push_back(pair.first);
            boardThemeFolders.push_back(pair.second);
        }
    }
    if (pieceThemeFolders.empty()) {
        pieceThemeFolders.push_back("assets/pieces/");
        boardThemeFolders.push_back("assets/board/");
    }

    boardWhitePreviews.clear();
    boardBlackPreviews.clear();
    themePreviewPieces.clear();

    const char* pieceNames[12] = {
        "white_king", "white_queen", "white_bishop",
        "white_knight", "white_rook", "white_pawn",
        "black_king", "black_queen", "black_bishop",
        "black_knight", "black_rook", "black_pawn"
    };

    for (size_t i = 0; i < pieceThemeFolders.size(); i++) {
        std::vector<sf::Texture> set(12);
        for (int k = 0; k < 12; k++)
            static_cast<void>(set[k].loadFromFile(pieceThemeFolders[i] + pieceNames[k] + ".png"));
        themePreviewPieces.push_back(std::move(set));

        sf::Texture whitePreview;
        sf::Texture blackPreview;
        static_cast<void>(whitePreview.loadFromFile(boardThemeFolders[i] + "white_tile.png"));
        static_cast<void>(blackPreview.loadFromFile(boardThemeFolders[i] + "black_tile.png"));
        boardWhitePreviews.push_back(std::move(whitePreview));
        boardBlackPreviews.push_back(std::move(blackPreview));
    }

    if (pieceTheme >= static_cast<int>(pieceThemeFolders.size()))
        pieceTheme = 0;
    boardTheme = pieceTheme;
}

void ChessUI::refreshBoardThemes() {
    if (boardThemeFolders.empty())
        refreshPieceThemes();
    boardTheme = pieceTheme;
}

void ChessUI::loadBoardTextures() {
    const std::string& folder = boardThemeFolders[boardTheme];
    static_cast<void>(whiteTileTexture.loadFromFile(folder + "white_tile.png"));
    static_cast<void>(blackTileTexture.loadFromFile(folder + "black_tile.png"));
}

void ChessUI::loadPieceTextures() {
    const std::string& folder = pieceThemeFolders[pieceTheme];

    static_cast<void>(pieceTextures[1].loadFromFile(folder + "white_pawn.png"));
    static_cast<void>(pieceTextures[2].loadFromFile(folder + "white_knight.png"));
    static_cast<void>(pieceTextures[3].loadFromFile(folder + "white_bishop.png"));
    static_cast<void>(pieceTextures[4].loadFromFile(folder + "white_rook.png"));
    static_cast<void>(pieceTextures[5].loadFromFile(folder + "white_queen.png"));
    static_cast<void>(pieceTextures[6].loadFromFile(folder + "white_king.png"));

    static_cast<void>(pieceTextures[-1].loadFromFile(folder + "black_pawn.png"));
    static_cast<void>(pieceTextures[-2].loadFromFile(folder + "black_knight.png"));
    static_cast<void>(pieceTextures[-3].loadFromFile(folder + "black_bishop.png"));
    static_cast<void>(pieceTextures[-4].loadFromFile(folder + "black_rook.png"));
    static_cast<void>(pieceTextures[-5].loadFromFile(folder + "black_queen.png"));
    static_cast<void>(pieceTextures[-6].loadFromFile(folder + "black_king.png"));
}

void ChessUI::drawLegalMoves() {
    for (const auto& move : legalMoves) {
        int row = move.first;
        int col = move.second;
        auto boardRow = game.begin() + row;
        int pieceOnSquare = (*boardRow)[col];

        if (pieceOnSquare == 0) {
            sf::CircleShape dot(10.f);
            dot.setOrigin({10.f, 10.f});
            int shownCol = displayCol(col);
            int shownRow = displayRow(row);
            dot.setPosition({ boardX + shownCol * squareSize + squareSize / 2.f, boardY + shownRow * squareSize + squareSize / 2.f});
            dot.setFillColor( sf::Color(40, 40, 40, 110) );
            window.draw(dot);
        }

        else {
            sf::CircleShape ring(32.f);
            ring.setOrigin({32.f, 32.f});
            int shownCol = displayCol(col);
            int shownRow = displayRow(row);
            ring.setPosition({ boardX + shownCol * squareSize + squareSize / 2.f, boardY + shownRow * squareSize + squareSize / 2.f });
            ring.setFillColor(sf::Color::Transparent);
            ring.setOutlineColor( sf::Color(180, 40, 40, 180) );
            ring.setOutlineThickness(5.f);
            window.draw(ring);
        }
    }
}

void ChessUI::drawMoveHistory() {
    if (historyTree.empty())
        return;

    const float iconSize = 45.f;
    std::vector<sf::Vector2f> positions = getHistoryNodePositions();
    const HistoryScrollBar verticalBar = getHistoryScrollBar();
    const float historyLeft = sidePanelX + 20.f;
    const float historyRight = verticalBar.x - 8.f;
    const float historyTop = verticalBar.y;
    const float historyBottom = verticalBar.y + verticalBar.height;

    const sf::Vector2f pinnedPos{sidePanelX + 25.f, sidePanelY + 75.f};
    auto nodePos = [&](int idx) -> sf::Vector2f {
        if (idx == 0) return pinnedPos;
        return {positions[idx].x, positions[idx].y - historyScroll};
    };

    sf::RectangleShape pinnedMask({historyRight - historyLeft, iconSize + 6.f});
    pinnedMask.setPosition({historyLeft, pinnedPos.y - 3.f});
    pinnedMask.setFillColor(sf::Color(45, 45, 45));
    window.draw(pinnedMask);

    {
        const sf::View savedView = window.getView();
        const sf::FloatRect sceneViewport = logicalView.getViewport();
        const float worldW = static_cast<float>(windowWidth);
        const float worldH = static_cast<float>(windowHeight);

        sf::View clipView(sf::FloatRect(
            {historyLeft, historyTop},
            {historyRight - historyLeft, historyBottom - historyTop}));
        clipView.setViewport(sf::FloatRect(
            {sceneViewport.position.x + (historyLeft / worldW) * sceneViewport.size.x,
             sceneViewport.position.y + (historyTop / worldH) * sceneViewport.size.y},
            {((historyRight - historyLeft) / worldW) * sceneViewport.size.x,
             ((historyBottom - historyTop) / worldH) * sceneViewport.size.y}));
        window.setView(clipView);

        for (int i = 0; i < static_cast<int>(historyTree.size()); i++) {
            int parent = historyTree[i].parent;

            if (parent < 0)
                continue;

            const sf::Vector2f p1 = nodePos(parent);
            const sf::Vector2f p2 = nodePos(i);
            float x1 = p1.x + iconSize / 2.f;
            float y1 = p1.y + iconSize / 2.f;

            float x2 = p2.x + iconSize / 2.f;
            float y2 = p2.y + iconSize / 2.f;

            float dx = x2 - x1;
            float dy = y2 - y1;

            float length = std::sqrt(dx * dx + dy * dy);
            float angle = std::atan2(dy, dx) * 180.f / 3.14159265f;

            sf::RectangleShape line({length, 3.f});
            line.setOrigin({0.f, 1.5f});
            line.setPosition({x1, y1});
            line.setRotation(sf::degrees(angle));
            line.setFillColor(sf::Color(120, 120, 120));

            window.draw(line);
        }

        for (int i = 0; i < static_cast<int>(historyTree.size()); i++) {
            const sf::Vector2f p = nodePos(i);
            float x = p.x;
            float y = p.y;

            bool currentNode = i == viewingHistoryNode;

            sf::RectangleShape nodeBox({iconSize, iconSize});
            nodeBox.setPosition({x, y});
            nodeBox.setFillColor(sf::Color(55, 55, 55));

            if (currentNode) {
                nodeBox.setOutlineColor(sf::Color(255, 215, 0));
                nodeBox.setOutlineThickness(3.f);
            }

            window.draw(nodeBox);

            const MoveHistoryEntry& move = historyTree[i].move;

            if (historyStyle == HistoryStyle::Pictogram) {
                int piece = move.piece;
                sf::Sprite sprite(pieceTextures[piece]);

                auto textureSize = pieceTextures[piece].getSize();
                float maxSize = static_cast<float>( std::max(textureSize.x, textureSize.y) );
                float scale = 36.f / maxSize;

                sprite.setScale({scale, scale});

                float width = textureSize.x * scale;
                float height = textureSize.y * scale;

                sprite.setPosition({
                    x + (iconSize - width) / 2.f,
                    y + (iconSize - height) / 2.f
                });

                window.draw(sprite);
            }

            else {
                sf::Text moveText(font, moveToText(move), 11);
                moveText.setPosition({x + 3.f, y + 14.f});
                moveText.setFillColor(sf::Color::White);

                window.draw(moveText);
            }
        }

        window.setView(savedView);
    }

    const HistoryHorizontalScrollBar horizontalBar = getHistoryHorizontalScrollBar();
    if (horizontalBar.maxScroll > 0.f) {
        sf::RectangleShape track({horizontalBar.width, 7.f});
        track.setPosition({horizontalBar.x, horizontalBar.y});
        track.setFillColor(sf::Color(70, 70, 70));
        window.draw(track);

        sf::RectangleShape thumb({horizontalBar.thumbWidth, 9.f});
        thumb.setPosition({horizontalBar.thumbX, horizontalBar.y - 1.f});
        thumb.setFillColor(sf::Color(175, 175, 175));
        window.draw(thumb);
    }

    if (currentScreen == Screen::Game && verticalBar.maxScroll > 0.f) {
        sf::RectangleShape track({12.f, verticalBar.height});
        track.setPosition({verticalBar.x, verticalBar.y});
        track.setFillColor(sf::Color(88, 88, 88));
        window.draw(track);

        sf::RectangleShape thumb({12.f, verticalBar.thumbHeight});
        thumb.setPosition({verticalBar.x, verticalBar.thumbY});
        thumb.setFillColor(sf::Color(215, 215, 215));
        window.draw(thumb);
    }
}

void ChessUI::drawHistoryPreview() {
    if (!historyPreviewHeld) {
        hoveredHistoryNode = -1;
        return;
    }

    const HistoryScrollBar verticalBar = getHistoryScrollBar();
    const float historyLeft = sidePanelX + 20.f;
    const float historyRight = verticalBar.x - 8.f;
    const float historyTop = verticalBar.y;
    const float historyBottom = verticalBar.y + verticalBar.height;

    bool mouseInsideHistory =
        mousePosition.x >= historyLeft &&
        mousePosition.x <= historyRight &&
        mousePosition.y >= historyTop &&
        mousePosition.y <= historyBottom;

    if (!mouseInsideHistory) {
        hoveredHistoryNode = -1;
        return;
    }

    if (historyTree.empty())
        return;

    const float iconSize = 45.f;
    std::vector<sf::Vector2f> positions = getHistoryNodePositions();
    const sf::Vector2f pinnedPos{sidePanelX + 25.f, sidePanelY + 75.f};
    auto nodePos = [&](int idx) -> sf::Vector2f {
        if (idx == 0) return pinnedPos;
        return {positions[idx].x, positions[idx].y - historyScroll};
    };

    for (int i = 0; i < static_cast<int>(historyTree.size()); i++) {
        const sf::Vector2f p = nodePos(i);
        float x = p.x;
        float y = p.y;

        if (i != 0 && (x < historyLeft || x + iconSize > historyRight))
            continue;

        if (i != 0 && (y < historyTop || y + iconSize > historyBottom))
            continue;

        if (mousePosition.x >= x &&
            mousePosition.x <= x + iconSize &&
            mousePosition.y >= y &&
            mousePosition.y <= y + iconSize) {
            hoveredHistoryNode = i;
            break;
        }
    }

    if (hoveredHistoryNode == -1)
        return;

    int parent = historyTree[hoveredHistoryNode].parent;

    Board previewBoard = parent == -1
        ? startingBoard
        : historyTree[parent].move.boardAfterMove;

    drawBoardPosition(previewBoard);
    drawMoveArrow(historyTree[hoveredHistoryNode].move);
}

int ChessUI::getHistoryDepth(int nodeIndex) const {
    int depth = 0;
    int node = nodeIndex;

    while (node >= 0 && historyTree[node].parent >= 0) {
        depth++;
        node = historyTree[node].parent;
    }

    return depth;
}

std::vector<sf::Vector2f> ChessUI::getHistoryNodePositions() const {
    std::vector<sf::Vector2f> positions(historyTree.size());

    if (historyTree.empty())
        return positions;

    std::vector<int> branchColumn(historyTree.size(), 0);
    int nextFreeColumn = 1;

    for (int i = 0; i < static_cast<int>(historyTree.size()); i++) {
        int parent = historyTree[i].parent;

        if (parent < 0) {
            branchColumn[i] = 0;
            continue;
        }

        const std::vector<int>& siblings = historyTree[parent].children;

        int childNumber = 0;

        for (int j = 0; j < static_cast<int>(siblings.size()); j++) {
            if (siblings[j] == i) {
                childNumber = j;
                break;
            }
        }

        if (childNumber == 0) {
            branchColumn[i] = branchColumn[parent];
        }

        else {
            branchColumn[i] = nextFreeColumn;
            nextFreeColumn++;
        }
    }

    const float startX = sidePanelX + 25.f;
    const float startY = sidePanelY + 75.f;
    const float branchSpacing = 62.f;
    const float rowSpacing = 70.f;

    for (int i = 0; i < static_cast<int>(historyTree.size()); i++) {
        int depth = getHistoryDepth(i);

        positions[i] = {
            startX + branchColumn[i] * branchSpacing - historyHorizontalScroll,
            startY + depth * rowSpacing
        };
    }

    return positions;
}

ChessUI::HistoryScrollBar ChessUI::getHistoryScrollBar() const {
    const float trackY = sidePanelY + 70.f;
    const float trackHeight = sidePanelHeight - 110.f;
    int maxDepth = -1;
    for (int i = 0; i < static_cast<int>(historyTree.size()); ++i)
        maxDepth = std::max(maxDepth, getHistoryDepth(i));

    const float contentBottom = sidePanelY + 75.f + maxDepth * 70.f + 45.f;
    const float maxScroll = std::max(0.f, contentBottom - trackY - trackHeight);
    const float contentHeight = trackHeight + maxScroll;
    const float thumbHeight = maxScroll > 0.f
        ? std::min(trackHeight * 0.75f, std::max(54.f, trackHeight * trackHeight / contentHeight))
        : trackHeight;
    const float thumbTravel = trackHeight - thumbHeight;
    const float scroll = std::clamp(historyScroll, 0.f, maxScroll);
    const float thumbY = trackY + (maxScroll > 0.f
        ? scroll / maxScroll * thumbTravel : 0.f);

    return {sidePanelX + sidePanelWidth - 20.f, trackY, trackHeight, thumbY, thumbHeight, maxScroll};
}

ChessUI::HistoryHorizontalScrollBar ChessUI::getHistoryHorizontalScrollBar() const {
    constexpr float iconSize = 45.f;
    const HistoryScrollBar verticalBar = getHistoryScrollBar();
    const float visibleLeft = sidePanelX + 20.f;
    const float visibleWidth = verticalBar.x - 8.f - visibleLeft;
    float contentRight = visibleLeft;
    for (const auto& position : getHistoryNodePositions())
        contentRight = std::max(contentRight, position.x + historyHorizontalScroll + iconSize);

    const float contentWidth = std::max(visibleWidth, contentRight - visibleLeft);
    const float maxScroll = std::max(0.f, contentWidth - visibleWidth);
    const float thumbWidth = std::min(visibleWidth, std::max(45.f, visibleWidth * visibleWidth / contentWidth));
    const float thumbTravel = visibleWidth - thumbWidth;
    const float thumbX = visibleLeft + (maxScroll > 0.f ? historyHorizontalScroll / maxScroll * thumbTravel : 0.f);
    return {visibleLeft, sidePanelY + sidePanelHeight - 18.f, visibleWidth, thumbX, thumbWidth, maxScroll};
}

void ChessUI::scrollHistoryHorizontalFromMouse(float mouseX) {
    const HistoryHorizontalScrollBar bar = getHistoryHorizontalScrollBar();
    const float thumbTravel = bar.width - bar.thumbWidth;
    historyHorizontalScroll = thumbTravel > 0.f
        ? std::clamp((mouseX - historyHorizontalScrollbarGrabOffset - bar.x) / thumbTravel * bar.maxScroll, 0.f, bar.maxScroll)
        : 0.f;
}

void ChessUI::drawSettings() {
    sf::RectangleShape windowBox({1200.f, 800.f});
    windowBox.setPosition({0.f, 0.f});
    windowBox.setFillColor(sf::Color(12, 13, 15));
    window.draw(windowBox);

    sf::Text title(font, "SETTINGS", 34);
    title.setPosition({72.f, 38.f});
    title.setFillColor(sf::Color::White);
    window.draw(title);

    sf::Text backArrow(font, "<", 30);
    backArrow.setPosition({36.f, 37.f});
    backArrow.setFillColor(sf::Color(190, 192, 196));
    window.draw(backArrow);

    sf::Text description(font,
        "Adjust how you move pieces and view the chessboard.", 14);
    description.setPosition({74.f, 91.f});
    description.setFillColor(sf::Color(160, 163, 169));
    window.draw(description);

    sf::RectangleShape gameplayTab({140.f, 38.f});
    gameplayTab.setPosition({812.f, 42.f});
    gameplayTab.setFillColor(settingsTab == 0
        ? sf::Color(46, 47, 50) : sf::Color(25, 26, 28));
    gameplayTab.setOutlineColor(sf::Color(55, 57, 61));
    gameplayTab.setOutlineThickness(1.f);
    window.draw(gameplayTab);

    sf::Text gameplayText(font, "GAMEPLAY", 16);
    gameplayText.setPosition({832.f, 52.f});
    gameplayText.setFillColor(sf::Color::White);
    window.draw(gameplayText);

    sf::RectangleShape designTab({140.f, 38.f});
    designTab.setPosition({962.f, 42.f});
    designTab.setFillColor(settingsTab == 1
        ? sf::Color(46, 47, 50) : sf::Color(25, 26, 28));
    designTab.setOutlineColor(sf::Color(55, 57, 61));
    designTab.setOutlineThickness(1.f);
    window.draw(designTab);

    sf::Text designText(font, "DESIGN", 16);
    designText.setPosition({1008.f, 52.f});
    designText.setFillColor(sf::Color::White);
    window.draw(designText);

    if (settingsTab == 0)
        drawGameplaySettings();
    else
        drawDesignSettings();

    if (gameStarted) {
        sf::RectangleShape playAgainButton({150.f, 42.f});
        playAgainButton.setPosition({350.f, 662.f});
        playAgainButton.setFillColor(sf::Color(70, 120, 75));
        window.draw(playAgainButton);

        sf::Text playAgainText(font, "PLAY AGAIN", 14);
        playAgainText.setPosition({374.f, 676.f});
        playAgainText.setFillColor(sf::Color::White);
        window.draw(playAgainText);
    }

    sf::RectangleShape menuButton({150.f, 42.f});
    menuButton.setPosition({525.f, 662.f});
    menuButton.setFillColor(sf::Color(65, 65, 65));
    window.draw(menuButton);

    sf::Text menuText(font, "SIDE SELECT", 14);
    menuText.setPosition({542.f, 676.f});
    menuText.setFillColor(sf::Color::White);
    window.draw(menuText);

    sf::RectangleShape backButton({150.f, 42.f});
    backButton.setPosition({700.f, 662.f});
    backButton.setFillColor(sf::Color(55, 55, 55));
    window.draw(backButton);

    sf::Text backText(font, "BACK", 14);
    backText.setPosition({756.f, 676.f});
    backText.setFillColor(sf::Color::White);
    window.draw(backText);
}

void ChessUI::drawGameplaySettings() {
    auto drawRow = [&](const std::string& label, float y) {
        sf::Text labelText(font, label, 18);
        labelText.setPosition({160.f, y + 13.f});
        labelText.setFillColor(sf::Color(238, 239, 242));
        window.draw(labelText);

        sf::RectangleShape separator({920.f, 1.f});
        separator.setPosition({140.f, y + 49.f});
        separator.setFillColor(sf::Color(47, 49, 53));
        window.draw(separator);
    };

    auto drawToggle = [&](const std::string& label, bool enabled, float y) {
        drawRow(label, y);
        sf::RectangleShape toggle({86.f, 30.f});
        toggle.setPosition({944.f, y + 9.f});
        toggle.setFillColor(enabled
            ? sf::Color(63, 128, 78)
            : sf::Color(78, 82, 88));
        toggle.setOutlineColor(enabled
            ? sf::Color(91, 157, 104)
            : sf::Color(104, 108, 114));
        toggle.setOutlineThickness(1.f);
        window.draw(toggle);

        sf::Text state(font, enabled ? "ON" : "OFF", 13);
        state.setPosition({enabled ? 977.f : 972.f, y + 16.f});
        state.setFillColor(sf::Color::White);
        window.draw(state);
    };

    drawRow("Player movement", 205.f);
    auto drawMoveMode = [&](const std::string& label, float x, float width,
                            float textWidth, PlayerMoveMode mode) {
        const bool selected = playerMoveMode == mode;
        sf::RectangleShape button({width, 32.f});
        button.setPosition({x, 214.f});
        button.setFillColor(selected ? sf::Color(63, 128, 78) : sf::Color(78, 82, 88));
        button.setOutlineColor(selected ? sf::Color(91, 157, 104) : sf::Color(104, 108, 114));
        button.setOutlineThickness(1.f);
        window.draw(button);

        sf::Text text(font, label, 12);
        text.setPosition({x + (width - textWidth) / 2.f, 222.f});
        text.setFillColor(sf::Color::White);
        window.draw(text);
    };

    drawMoveMode("SLIDE", 650.f, 125.f, 36.f, PlayerMoveMode::Slide);
    drawMoveMode("DRAG", 785.f, 125.f, 34.f, PlayerMoveMode::Drag);
    drawMoveMode("POINT N CLICK", 920.f, 125.f, 98.f, PlayerMoveMode::PointAndClick);

    drawToggle("Show possible moves", showPossibleMoves, 270.f);
    drawToggle("Board coordinates", showCoordinates, 335.f);
    drawToggle("Sound effects", soundEffects, 400.f);

    drawRow("Move history", 465.f);
    sf::RectangleShape historyToggle({86.f, 30.f});
    historyToggle.setPosition({944.f, 474.f});
    historyToggle.setFillColor(sf::Color(78, 82, 88));
    historyToggle.setOutlineColor(sf::Color(104, 108, 114));
    historyToggle.setOutlineThickness(1.f);
    window.draw(historyToggle);

    sf::Text historyState(
        font,
        historyStyle == HistoryStyle::Pictogram ? "ICONS" : "TEXT",
        12
    );
    historyState.setPosition({historyStyle == HistoryStyle::Pictogram ? 967.f : 971.f, 482.f});
    historyState.setFillColor(sf::Color::White);
    window.draw(historyState);

    drawToggle("Fullscreen (F11)", fullscreen, 530.f);
    drawToggle("Evaluation bar", showEvaluationBar, 595.f);
}

void ChessUI::drawDesignSettings() {
    sf::Text title(font, "Game theme  -  pieces and board come as a pair", 20);
    title.setPosition({140.f, 200.f});
    title.setFillColor(sf::Color(220, 220, 220));
    window.draw(title);

    constexpr float cardWidth = 230.f;
    constexpr float cardHeight = 340.f;
    constexpr float cardStep = 250.f;
    constexpr float cardStartX = 135.f;
    constexpr float cardStartY = 235.f;
    constexpr float tile = 70.f;
    constexpr float pieceMax = 64.f;
    static const char* themeNames[] = {"Classic", "Pixel", "Latte", "Cat"};

    for (int i = 0; i < static_cast<int>(pieceThemeFolders.size()); i++) {
        const float x = cardStartX + i * cardStep;
        const float y = cardStartY;
        const bool selected = (i == pieceTheme);

        sf::RectangleShape card({cardWidth, cardHeight});
        card.setPosition({x, y});
        card.setFillColor(selected ? sf::Color(48, 70, 56) : sf::Color(35, 37, 40));
        card.setOutlineColor(selected ? sf::Color(255, 215, 0) : sf::Color(72, 75, 80));
        card.setOutlineThickness(selected ? 3.f : 1.f);
        window.draw(card);

        sf::Text name(font, i < 4 ? themeNames[i] : "Theme " + std::to_string(i + 1), 18);
        name.setPosition({x + 12.f, y + 8.f});
        name.setFillColor(sf::Color::White);
        window.draw(name);

        const float bx = x + 5.f;
        const float by = y + 36.f;

        for (int row = 0; row < 4; row++) {
            for (int col = 0; col < 3; col++) {
                const bool light = (row + col) % 2 == 0;
                const sf::Texture& tileTex = light ? boardWhitePreviews[i] : boardBlackPreviews[i];
                sf::Sprite square(tileTex);
                const sf::Vector2u tileSize = tileTex.getSize();
                square.setScale({tile / tileSize.x, tile / tileSize.y});
                square.setPosition({bx + col * tile, by + row * tile});
                window.draw(square);

                const sf::Texture& pieceTex = themePreviewPieces[i][row * 3 + col];
                const sf::Vector2u pieceSize = pieceTex.getSize();
                const float maxSize = static_cast<float>(std::max(pieceSize.x, pieceSize.y));
                if (maxSize <= 0.f)
                    continue;
                const float scale = pieceMax / maxSize;
                sf::Sprite piece(pieceTex);
                piece.setScale({scale, scale});
                piece.setPosition({
                    bx + col * tile + (tile - pieceSize.x * scale) / 2.f,
                    by + row * tile + (tile - pieceSize.y * scale) / 2.f
                });
                window.draw(piece);
            }
        }

        sf::Text state(font, selected ? "SELECTED" : "Click to use", 13);
        state.setPosition({x + 12.f, y + cardHeight - 24.f});
        state.setFillColor(selected ? sf::Color(190, 230, 190) : sf::Color(170, 174, 181));
        window.draw(state);
    }
}

void ChessUI::drawEndScreen() {
    sf::RectangleShape dimmer({1200.f, 800.f});
    dimmer.setPosition({0.f, 0.f});
    dimmer.setFillColor(sf::Color(0, 0, 0, 155));
    window.draw(dimmer);

    sf::RectangleShape endBox({520.f, 550.f});
    endBox.setPosition({340.f, 140.f});
    endBox.setFillColor(sf::Color(45, 45, 45));
    window.draw(endBox);

    sf::Text title(font, "GAME OVER", 48);
    title.setPosition({465.f, 235.f});
    title.setFillColor(sf::Color::White);
    window.draw(title);

    std::string endMessage = "The game has ended.";
    if (timeExpired) {
        endMessage = (timeoutLoser == 0 ? "Black" : "White");
        endMessage += " wins on time.";
    }
    sf::Text message(font, endMessage, 24);
    message.setPosition({477.f, 330.f});
    message.setFillColor(sf::Color(210, 210, 210));
    window.draw(message);

    if (!matchSaveMessage.empty()) {
        sf::Text saveError(font, matchSaveMessage, 16);
        saveError.setPosition({420.f, 390.f});
        saveError.setFillColor(sf::Color(245, 170, 135));
        window.draw(saveError);
    }

    sf::RectangleShape playAgain({200.f, 65.f});
    playAgain.setPosition({390.f, 500.f});
    playAgain.setFillColor(sf::Color(70, 120, 75));
    window.draw(playAgain);

    sf::Text playAgainText(font, "PLAY AGAIN", 20);
    playAgainText.setPosition({435.f, 520.f});
    playAgainText.setFillColor(sf::Color::White);
    window.draw(playAgainText);

    sf::RectangleShape settings({200.f, 65.f});
    settings.setPosition({610.f, 500.f});
    settings.setFillColor(sf::Color(65, 65, 65));
    window.draw(settings);

    sf::Text settingsText(font, "SETTINGS", 20);
    settingsText.setPosition({665.f, 520.f});
    settingsText.setFillColor(sf::Color::White);
    window.draw(settingsText);

    sf::RectangleShape menu({200.f, 60.f});
    menu.setPosition({500.f, 590.f});
    menu.setFillColor(sf::Color(55, 55, 55));
    window.draw(menu);

    sf::Text menuText(font, "SIDE SELECT", 18);
    menuText.setPosition({538.f, 608.f});
    menuText.setFillColor(sf::Color::White);
    window.draw(menuText);
}

void ChessUI::drawCapturedPieces() {
    std::vector<int> capturedByWhite;
    std::vector<int> capturedByBlack;

    for (const auto& move : moveHistory) {
        if (move.capturedPiece == 0)
            continue;

        if (move.capturedPiece < 0)
            capturedByWhite.push_back(move.capturedPiece);
        else
            capturedByBlack.push_back(move.capturedPiece);
    }

    sf::Text whiteLabel(font, "White captured:", 14);
    whiteLabel.setPosition({50.f, 726.f});
    whiteLabel.setFillColor(sf::Color::White);
    window.draw(whiteLabel);

    sf::Text blackLabel(font, "Black captured:", 14);
    blackLabel.setPosition({50.f, 760.f});
    blackLabel.setFillColor(sf::Color::White);
    window.draw(blackLabel);

    auto drawCapturedRow = [&](const std::vector<int>& pieces, float y) {
        float x = 175.f;

        std::vector<int> kinds;
        std::map<int, int> counts;
        for (int piece : pieces) {
            if (counts[piece]++ == 0)
                kinds.push_back(piece);
        }

        for (int piece : kinds) {
            sf::Sprite sprite(pieceTextures[piece]);

            auto textureSize = pieceTextures[piece].getSize();
            float maxSize = static_cast<float>(std::max(textureSize.x, textureSize.y));
            float scale = 25.f / maxSize;

            sprite.setScale({scale, scale});

            float width = textureSize.x * scale;
            float height = textureSize.y * scale;

            sprite.setPosition({x, y + (25.f - height) / 2.f});
            window.draw(sprite);

            x += width + 4.f;

            if (counts[piece] > 1) {
                sf::Text count(font, "X" + std::to_string(counts[piece]), 14);
                count.setPosition({x, y + 4.f});
                count.setFillColor(sf::Color::White);
                window.draw(count);
                x += count.getLocalBounds().size.x + 10.f;
            }
        }
    };

    drawCapturedRow(capturedByWhite, 724.f);
    drawCapturedRow(capturedByBlack, 758.f);
}

void ChessUI::drawCoordinates() {
    for (int col = 0; col < 8; col++) {
        char fileLetter = static_cast<char>('a' + col);

        sf::Text fileText(font, std::string(1, fileLetter), 13);

        int shownCol = displayCol(col);

        fileText.setPosition({
            static_cast<float>(boardX + shownCol * squareSize + squareSize - 14),
            static_cast<float>(boardY + boardSize - 18)
        });

        fileText.setFillColor(sf::Color(35, 35, 35, 210));
        window.draw(fileText);
    }

    for (int row = 0; row < 8; row++) {
        char rankNumber = static_cast<char>('8' - row);

        sf::Text rankText(font, std::string(1, rankNumber), 13);

        int shownRow = displayRow(row);

        rankText.setPosition({
            static_cast<float>(boardX + 5),
            static_cast<float>(boardY + shownRow * squareSize + 2)
        });

        rankText.setFillColor(sf::Color(35, 35, 35, 210));
        window.draw(rankText);
    }
}

void ChessUI::playSound(const sf::SoundBuffer& buffer) {
    if (!soundEffects)
        return;

    if (buffer.getDuration() == sf::Time::Zero)
        return;

    activeSound.emplace(buffer);
    activeSound->play();
}

void ChessUI::drawBoardPosition(Board& board) {
    drawBoard();

    if (showCoordinates)
        drawCoordinates();

    int row = 0;

    for (auto& boardRow : board) {
        int col = 0;

        for (auto& piece : boardRow) {
            if (piece != 0) {
                sf::Sprite sprite(pieceTextures[piece]);

                auto textureSize = pieceTextures[piece].getSize();
                float maxSize = static_cast<float>( std::max(textureSize.x, textureSize.y) );
                float scale = 64.f / maxSize;

                sprite.setScale({scale, scale});

                float width = textureSize.x * scale;
                float height = textureSize.y * scale;

                int shownCol = displayCol(col);
                int shownRow = displayRow(row);

                float x = boardX + shownCol * squareSize + (squareSize - width) / 2.f;
                float y = boardY + shownRow * squareSize + (squareSize - height) / 2.f;

                sprite.setPosition({x, y});
                window.draw(sprite);
            }

            col++;
        }

        row++;
    }
}

void ChessUI::drawMoveArrow(const MoveHistoryEntry& move) {
    float startArrowX = boardX + displayCol(move.fromCol) * squareSize + squareSize / 2.f;
    float startArrowY = boardY + displayRow(move.fromRow) * squareSize + squareSize / 2.f;

    float endArrowX = boardX + displayCol(move.toCol) * squareSize + squareSize / 2.f;
    float endArrowY = boardY + displayRow(move.toRow) * squareSize + squareSize / 2.f;

    float dx = endArrowX - startArrowX;
    float dy = endArrowY - startArrowY;

    float length = std::sqrt(dx * dx + dy * dy);
    float angle = std::atan2(dy, dx) * 180.f / 3.14159265f;

    const float arrowThickness = 7.f;
    const float arrowHeadLength = 24.f;

    sf::RectangleShape arrowLine({length, arrowThickness});
    arrowLine.setOrigin({0.f, arrowThickness / 2.f});
    arrowLine.setPosition({startArrowX, startArrowY});
    arrowLine.setRotation(sf::degrees(angle));
    arrowLine.setFillColor(sf::Color(220, 40, 40, 220));
    window.draw(arrowLine);

    sf::RectangleShape arrowHead1({arrowHeadLength, arrowThickness});
    arrowHead1.setOrigin({0.f, arrowThickness / 2.f});
    arrowHead1.setPosition({endArrowX, endArrowY});
    arrowHead1.setRotation(sf::degrees(angle + 150.f));
    arrowHead1.setFillColor(sf::Color(220, 40, 40, 220));
    window.draw(arrowHead1);

    sf::RectangleShape arrowHead2({arrowHeadLength, arrowThickness});
    arrowHead2.setOrigin({0.f, arrowThickness / 2.f});
    arrowHead2.setPosition({endArrowX, endArrowY});
    arrowHead2.setRotation(sf::degrees(angle - 150.f));
    arrowHead2.setFillColor(sf::Color(220, 40, 40, 220));
    window.draw(arrowHead2);
}

std::string ChessUI::moveToText(const MoveHistoryEntry& move) const {
    auto squareName = [](int row, int col) {
        std::string square;
        square += static_cast<char>('a' + col);
        square += static_cast<char>('8' - row);
        return square;
    };

    char pieceLetter = ' ';

    int piece = std::abs(move.piece);

    if (piece == 2) pieceLetter = 'N';
    if (piece == 3) pieceLetter = 'B';
    if (piece == 4) pieceLetter = 'R';
    if (piece == 5) pieceLetter = 'Q';
    if (piece == 6) pieceLetter = 'K';

    std::string text;

    if (pieceLetter != ' ')
        text += pieceLetter;

    text += squareName(move.fromRow, move.fromCol);

    text += move.capturedPiece != 0 ? "x" : "-";

    text += squareName(move.toRow, move.toCol);

    if (move.promotionPiece != 0) {
        const int promotedType = std::abs(move.promotionPiece);
        if (promotedType == 2) text += "=N";
        else if (promotedType == 3) text += "=B";
        else if (promotedType == 4) text += "=R";
        else if (promotedType == 5) text += "=Q";
    }

    return text;
}

void ChessUI::rebuildLinearHistoryFromNode(int nodeIndex) {
    moveHistory.clear();

    if (nodeIndex < 0) {
        currentHistoryIndex = -1;
        return;
    }

    std::vector<int> path;
    int node = nodeIndex;

    while (node >= 0) {
        path.push_back(node);
        node = historyTree[node].parent;
    }

    std::reverse(path.begin(), path.end());

    for (int index : path)
        moveHistory.push_back(historyTree[index].move);

    currentHistoryIndex = static_cast<int>(moveHistory.size()) - 1;

    historyScroll = std::min(historyScroll, getHistoryScrollBar().maxScroll);
}

void ChessUI::selectPiece(int row, int col) {
    pieceSelected = true;
    selectedRow = row;
    selectedCol = col;

    legalMoves.clear();

    if (!showPossibleMoves)
        return;

    for (int newRow = 0; newRow < 8; newRow++) {
        for (int newCol = 0; newCol < 8; newCol++) {
            if (game.valid_move(selectedRow, selectedCol, newRow, newCol)) {
                legalMoves.push_back({newRow, newCol});
            }
        }
    }
}

void ChessUI::clearSelection() {
    pieceSelected = false;
    selectedRow = -1;
    selectedCol = -1;

    legalMoves.clear();
}

void ChessUI::invalidateSearch() {
    if (searchStop) searchStop->store(true, std::memory_order_relaxed);
    ++positionGeneration;
    engineStalled = false;
    engineSearchFailed = false;
}

void ChessUI::startGame(int engineSide) {
    invalidateSearch();
    game = Board();
    startingBoard = game;

    this->engineSide = engineSide;
    gameAccountName = accountSignedIn ? accountName : "";
    gamePlayerSide = 1 - engineSide;
    gameRecorded = false;
    matchSaveMessage.clear();
    gameStarted = true;
    currentScreen = Screen::Game;
    evalStarted = false;

    moveHistory.clear();
    historyTree.clear();
    currentHistoryNode = -1;
    viewingHistoryNode = -1;

    legalMoves.clear();
    clearSelection();

    currentHistoryIndex = -1;
    historyScroll = 0.f;
    historyHorizontalScroll = 0.f;
    historyHorizontalScrollbarDragging = false;
    hoveredHistoryNode = -1;
    historyBrowsing = false;

    dragging = false;
    draggedRow = -1;
    draggedCol = -1;

    engineMoveWaiting = true;
    engineMoveClock.restart();
    engineMoveAnimating = false;
    animatedPiece = 0;

    pendingPromotion = Move{};

    moveTimeLeft = matchSeconds;
    clockKeyNode = -2;
    clockKeyTurn = -1;
    clockKeyMovesLeft = -1;
    timeExpired = false;
    timeoutLoser = -1;
    clockStarted = false;
    forceNewBranch = false;
    clockTimer.restart();
}
