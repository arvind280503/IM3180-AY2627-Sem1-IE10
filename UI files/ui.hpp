#pragma once
#include "../board.hpp"
#include "../engine.hpp"
#include <SFML/Graphics.hpp>
#include <SFML/Audio.hpp>
#include <map>
#include <utility>
#include <vector>
#include <string>
#include <optional>
#include <functional>
#include <future>
#include <atomic>
#include <memory>
#include <cstdint>

struct MoveHistoryEntry {
    int piece;

    int fromRow;
    int fromCol;

    int toRow;
    int toCol;

    int capturedPiece;

    int promotionPiece = 0;

    Board boardAfterMove;
};

class ChessUI {
    friend struct ChessUITestAccess;

public:
    struct MatchSummary {
        std::string white;
        std::string black;
        std::string result;
        std::string date;
        std::vector<Move> mainline;
    };

    using AuthHandler = std::function<bool(
        const std::string&, const std::string&, bool)>;
    using MatchHistoryProvider = std::function<std::vector<MatchSummary>(const std::string&)>;
    using MatchRecorder = std::function<void(
        const std::string&, int, const MatchSummary&, const std::vector<Move>&)>;

    explicit ChessUI(int engineSide = 1, int searchDepth = Engine::kDefaultSearchDepth,
                     std::unique_ptr<Evaluator> whiteEvaluator = nullptr,
                     std::unique_ptr<Evaluator> blackEvaluator = nullptr);
    ~ChessUI();
    void run();

    void setAuthHandler(AuthHandler handler) { authHandler = std::move(handler); }
    void setLogoutHandler(std::function<void()> handler) { logoutHandler = std::move(handler); }
    void setMatchRecorder(MatchRecorder recorder) { matchRecorder = std::move(recorder); }
    void setMatchHistory(std::vector<MatchSummary> matches) {
        matchSummaries = std::move(matches);
        matchHistoryMessage.clear();
    }
    void setMatchHistoryProvider(MatchHistoryProvider provider) {
        matchHistoryProvider = std::move(provider);
        matchHistoryMessage.clear();
    }

private:
    Board game;
    Engine whiteEngine;
    Engine blackEngine;
    struct SearchResult {
        bool found;
        Move move;
    };
    std::future<SearchResult> searchFuture;
    std::shared_ptr<std::atomic<bool>> searchStop;
    std::uint64_t positionGeneration = 0;
    std::uint64_t searchGeneration = 0;
    Hash searchRootHash = 0;
    int engineSide = 0;
    sf::RenderWindow window;

    enum class Screen { StartScreen, ChooseSide, Game, Promotion, Settings, EndGame, Account, MatchHistory };

    enum class PlayerMoveMode { Slide, Drag, PointAndClick };

    enum class HistoryStyle {
        Pictogram,
        Algebraic
    };

    struct HistoryNode {
        MoveHistoryEntry move;
        int parent = -1;
        std::vector<int> children;
    };

    struct HistoryScrollBar {
        float x, y, height, thumbY, thumbHeight, maxScroll;
    };

    struct HistoryHorizontalScrollBar {
        float x, y, width, thumbX, thumbWidth, maxScroll;
    };

    Screen currentScreen = Screen::StartScreen;
    Screen previousScreen = Screen::Game;
    Screen accountReturnScreen = Screen::ChooseSide;
    Screen matchHistoryReturnScreen = Screen::Account;
    HistoryStyle historyStyle = HistoryStyle::Pictogram;

    sf::Clock engineMoveClock;
    sf::Clock engineAnimationClock;
    bool engineMoveWaiting = false;
    bool engineMoveAnimating = false;
    int animatedFromRow = -1;
    int animatedFromCol = -1;
    int animatedToRow = -1;
    int animatedToCol = -1;
    int animatedPiece = 0;
    Move animatedMove{};
    PlayerMoveMode playerMoveMode = PlayerMoveMode::Drag;
    bool fullscreen = false;
    const float engineMoveDelaySeconds = 0.5f;
    const float engineSlideSeconds = 0.3f;

    const float matchSeconds = 120.f;
    float moveTimeLeft = 120.f;
    int clockKeyNode = -2;
    int clockKeyTurn = -1;
    int clockKeyMovesLeft = -1;
    sf::Clock clockTimer;
    bool timeExpired = false;
    int timeoutLoser = -1;
    bool clockStarted = false;
    void updateClocks();
    void drawClocks();

    bool historyPreviewHeld = false;
    bool historyBrowsing = false;
    bool forceNewBranch = false;

    Move pendingPromotion{};

    bool showPossibleMoves = true;
    bool pieceSelected = false;
    int selectedRow = -1;
    int selectedCol = -1;

    std::vector<HistoryNode> historyTree;
    int currentHistoryNode = -1;
    int viewingHistoryNode = -1;
    int hoveredHistoryNode = -1;
    float historyHorizontalScroll = 0.f;
    bool historyHorizontalScrollbarDragging = false;
    float historyHorizontalScrollbarGrabOffset = 0.f;

    int pieceTheme = 0;
    int settingsTab = 0;
    bool gameStarted = false;

    bool manualBoardFlip = false;
    bool showCoordinates = true;
    bool showEvaluationBar = true;
    bool soundEffects = true;
    bool accountSignUpMode = false;
    bool accountPasswordField = false;
    bool accountSignedIn = false;
    bool profilePicturePickerOpen = false;
    std::string accountName;
    std::string accountInput;
    std::string accountPassword;
    std::string accountMessage;
    std::string profilePicturePath;
    std::vector<std::string> profilePictureOptions;
    std::vector<sf::Texture> profilePictureOptionTextures;
    float profilePicturePickerScroll = 0.f;
    sf::Texture profilePictureTexture;
    AuthHandler authHandler;
    std::function<void()> logoutHandler;
    std::vector<MatchSummary> matchSummaries;
    std::string matchHistoryMessage;
    MatchHistoryProvider matchHistoryProvider;
    MatchRecorder matchRecorder;
    int matchHistoryPage = 0;
    const int matchHistoryRowsPerPage = 7;
    bool replayActive = false;
    MatchSummary replayMatch;
    int replayMoveIndex = -1;
    Board replayBoard;
    double evalShown = 0.0;
    double evalFrom = 0.0;
    double evalTo = 0.0;
    bool evalStarted = false;
    sf::Clock evalClock;
    const float evalSlideSeconds = 0.7f;
    std::string gameAccountName;
    int gamePlayerSide = -1;
    bool gameRecorded = false;
    std::string matchSaveMessage;
    sf::View logicalView;

    std::vector<std::string> pieceThemeFolders;
    std::vector<std::vector<sf::Texture>> themePreviewPieces;

    int boardTheme = 0;
    std::vector<std::string> boardThemeFolders;
    std::vector<sf::Texture> boardWhitePreviews;
    std::vector<sf::Texture> boardBlackPreviews;
    sf::Texture whiteTileTexture;
    sf::Texture blackTileTexture;

    sf::Texture selectionFrameTexture;
    sf::Texture startBackgroundTexture;
    sf::Clock selectionFrameClock;
    int selectionFrame = 1;
    const int selectionFrameCount = 96;

    sf::Font font;
    std::map<int, sf::Texture> pieceTextures;
    std::vector<std::pair<int, int>> legalMoves;
    std::vector<MoveHistoryEntry> moveHistory;
    float historyScroll = 0.f;
    bool dragging = false;
    int draggedRow = -1;
    int draggedCol = -1;
    int engineSearchDepth;
    bool engineStalled = false;
    bool engineSearchFailed = false;
    Board startingBoard;
    int currentHistoryIndex = -1;

    sf::Vector2f mousePosition;

    sf::SoundBuffer moveSoundBuffer;
    sf::SoundBuffer captureSoundBuffer;
    sf::SoundBuffer uiClickSoundBuffer;
    sf::SoundBuffer victorySoundBuffer;
    std::optional<sf::Sound> activeSound;

    const int windowWidth = 1200;
    const int windowHeight = 800;

    const int boardSize = 640;
    const int squareSize = boardSize / 8;

    const int boardX = 50;
    const int boardY = 80;

    const int sidePanelX = 740;
    const int sidePanelY = 80;
    const int sidePanelWidth = 400;
    const int sidePanelHeight = 640;

    void drawHistoryPreview();
    void drawMoveHistory();
    void drawLegalMoves();
    void loadPieceTextures();
    void handleEvents();
    void updateEngine();
    void invalidateSearch();
    bool applyMove(const Move& move);
    bool engineTurn() const;
    void updateResponsiveView(unsigned int width, unsigned int height);
    void toggleFullscreen();
    void drawLastMoveHighlights();
    void drawProfileButton();
    void drawAccountScreen();
    void drawProfilePicturePicker();
    void drawMatchHistoryScreen();
    void drawMatchHistoryPagination();
    void drawSmallButton(float x, float y, const std::string& label, bool enabled,
                         sf::Color fill = sf::Color(65, 65, 65));
    void drawReplayControls();
    void stepReplay(int delta);
    void updateEvaluationBar();
    void drawEvaluationBar();
    void submitAccountForm();
    void openMatchHistory();
    void recordCompletedMatch();
    void loadProfilePictureOptions();
    void draw();
    void drawPieces();
    void drawBoard();
    void drawSidePanel();
    void drawText();
    void updateSelectionVideo();
    void drawStartScreen();
    void drawChooseSide();
    void drawSettings();
    void drawGameplaySettings();
    void drawDesignSettings();
    void drawEndScreen();
    void drawPromotionDialog();
    void drawCapturedPieces();
    void drawCoordinates();
    void refreshPieceThemes();
    void refreshBoardThemes();
    void loadBoardTextures();
    void drawCoverTexture(const sf::Texture& texture);
    bool isBoardFlipped() const;
    int displayRow(int row) const;
    int displayCol(int col) const;
    std::vector<sf::Vector2f> getHistoryNodePositions() const;
    int getHistoryDepth(int nodeIndex) const;
    HistoryScrollBar getHistoryScrollBar() const;
    HistoryHorizontalScrollBar getHistoryHorizontalScrollBar() const;
    void scrollHistoryHorizontalFromMouse(float mouseX);
    void drawMoveArrow(const MoveHistoryEntry& move);
    void drawBoardPosition(Board& board);
    std::string moveToText(const MoveHistoryEntry& move) const;
    void rebuildLinearHistoryFromNode(int nodeIndex);
    void selectPiece(int row, int col);
    void clearSelection();
    void startGame(int engineSide);
    void playSound(const sf::SoundBuffer& buffer);
};
