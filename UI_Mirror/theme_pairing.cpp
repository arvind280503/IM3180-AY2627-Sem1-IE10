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

    pieceThemeKings.clear();
    boardWhitePreviews.clear();
    boardBlackPreviews.clear();
    for (size_t i = 0; i < pieceThemeFolders.size(); i++) {
        sf::Texture kingTexture;
        static_cast<void>(kingTexture.loadFromFile(pieceThemeFolders[i] + "white_king.png"));
        pieceThemeKings.push_back(std::move(kingTexture));

        sf::Texture whitePreview, blackPreview;
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

// Where a theme card is clicked, select both piece and board together:
//     pieceTheme = i;
//     boardTheme = i;
//     loadPieceTextures();
//     loadBoardTextures();
