// multiplayer.cpp - encoding and parsing for the LAN state stream.
#include "multiplayer.h"

#include <algorithm>
#include <sstream>

namespace tetris {

std::string EncodeState(const Game& game) {
    char state = 'P';
    switch (game.state()) {
        case Game::State::Playing: state = 'P'; break;
        case Game::State::Paused: state = 'U'; break;
        case Game::State::Clearing: state = 'C'; break;
        case Game::State::GameOver: state = 'O'; break;
    }

    std::string out;
    out.reserve(256);
    out += "STATE ";
    out += state;
    out += ' ';
    out += std::to_string(game.piece().type);
    out += ' ';
    out += std::to_string(game.piece().rot);
    out += ' ';
    out += std::to_string(game.piece().x);
    out += ' ';
    out += std::to_string(game.piece().y);
    out += ' ';
    out += std::to_string(game.score());
    out += ' ';
    out += std::to_string(game.level());
    out += ' ';
    out += std::to_string(game.lines());
    out += ' ';
    out += std::to_string(game.hold());
    out += ' ';

    for (int r = 0; r < kRows; ++r) {
        for (int c = 0; c < kCols; ++c) {
            out += static_cast<char>('0' + game.board()[r][c]);
        }
    }
    return out;
}

std::string EncodeGarbage(int lines) {
    return "GARBAGE " + std::to_string(std::max(0, lines));
}

std::string EncodeRestart() { return "RESTART"; }
std::string EncodeBye() { return "BYE"; }

bool ParseMessage(const std::string& line, Message& out) {
    out = Message{};
    std::istringstream in(line);
    std::string tag;
    if (!(in >> tag)) return false;

    if (tag == "STATE") {
        std::string state;
        int type = 0;
        int rot = 0;
        int x = 0;
        int y = 0;
        int score = 0;
        int level = 1;
        int lines = 0;
        int hold = 0;
        std::string board;
        if (!(in >> state >> type >> rot >> x >> y >> score >> level >> lines >> hold >>
              board)) {
            return false;
        }
        if (board.size() != static_cast<size_t>(kRows * kCols)) return false;

        RemotePlayer& player = out.state;
        player.active = true;
        player.paused = state == "U";
        player.clearing = state == "C";
        player.gameOver = state == "O";
        player.piece.type = (type >= 1 && type <= 7) ? type : 0;
        player.piece.rot = rot & 3;
        player.piece.x = x;
        player.piece.y = y;
        player.score = std::max(0, score);
        player.level = std::clamp(level, 1, kMaxLevel);
        player.lines = std::max(0, lines);
        player.hold = (hold >= 1 && hold <= 7) ? hold : 0;

        for (int r = 0; r < kRows; ++r) {
            for (int c = 0; c < kCols; ++c) {
                const char ch = board[static_cast<size_t>(r * kCols + c)];
                if (ch < '0' || ch > '8') return false;
                player.board[r][c] = static_cast<ColorIndex>(ch - '0');
            }
        }
        out.type = MessageType::State;
        return true;
    }

    if (tag == "GARBAGE") {
        int count = 0;
        if (!(in >> count)) return false;
        out.type = MessageType::Garbage;
        out.value = std::clamp(count, 0, kRows);
        return true;
    }

    if (tag == "RESTART") {
        out.type = MessageType::Restart;
        return true;
    }
    if (tag == "BYE") {
        out.type = MessageType::Bye;
        return true;
    }
    if (tag == "HELLO") {
        out.type = MessageType::Hello;
        return true;
    }
    if (tag == "START") {
        out.type = MessageType::Start;
        return true;
    }
    return false;
}

}  // namespace tetris
