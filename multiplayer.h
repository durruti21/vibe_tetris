// multiplayer.h - wire protocol shared by the two LAN players.
#pragma once

#include <string>

#include "game.h"

namespace tetris {

// Snapshot of the other player's playfield, received over the network.
struct RemotePlayer {
    bool active = false;
    bool paused = false;
    bool clearing = false;
    bool gameOver = false;
    ColorIndex board[kRows][kCols]{};
    Piece piece{};
    int score = 0;
    int level = 1;
    int lines = 0;
    int hold = 0;

    void Reset() { *this = RemotePlayer{}; }
};

enum class MessageType { None, State, Garbage, Restart, Bye, Hello, Start };

struct Message {
    MessageType type = MessageType::None;
    int value = 0;
    RemotePlayer state;
};

// Serialize the local game as one newline-free "STATE ..." line.
std::string EncodeState(const Game& game);
std::string EncodeGarbage(int lines);
std::string EncodeRestart();
std::string EncodeBye();

// Parse one line. Returns false for malformed or unknown messages.
bool ParseMessage(const std::string& line, Message& out);

}  // namespace tetris
