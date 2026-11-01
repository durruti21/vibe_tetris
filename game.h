// game.h - core Tetris-like game logic, independent of the windowing code
#pragma once

#include <cstdint>
#include <deque>
#include <random>
#include <string>
#include <vector>

namespace tetris {

inline constexpr int kCols = 10;
inline constexpr int kRows = 20;
inline constexpr int kPieceCount = 7;
inline constexpr int kMaxLevel = 20;

// 1 = I, 2 = J, 3 = L, 4 = O, 5 = S, 6 = T, 7 = Z (0 = none)
using ColorIndex = uint8_t;

struct Piece {
    int type = 0;
    int rot = 0;
    int x = 0;
    int y = 0;
};

class Game {
public:
    enum class State { Playing, Paused, Clearing, GameOver };
    enum class Action {
        LeftDown, LeftUp, RightDown, RightUp, SoftDown, SoftUp,
        RotateCW, RotateCCW, HardDrop, Hold, TogglePause, Restart
    };

    Game();

    void Reset();
    void Update(double dtMs);
    void Handle(Action action);
    void Pause();
    void TogglePause();
    void SetBest(int best);

    // Read-only accessors used by the renderer.
    State state() const { return state_; }
    const ColorIndex (&board() const)[kRows][kCols] { return board_; }
    const Piece& piece() const { return piece_; }
    const std::deque<int>& queue() const { return queue_; }
    int hold() const { return hold_; }
    int score() const { return score_; }
    int best() const { return best_; }
    int level() const { return level_; }
    int lines() const { return lines_; }
    int combo() const { return combo_; }
    bool newBest() const { return newBest_; }
    const std::vector<int>& clearingRows() const { return clearingRows_; }
    double clearProgress() const { return clearDuration_ > 0 ? clearTimer_ / clearDuration_ : 0.0; }
    const std::wstring& message() const { return message_; }
    double messageAlpha() const;

    // Geometry helpers shared with the renderer.
    bool Occupied(int row, int col) const { return board_[row][col] != 0; }
    int GhostY() const;
    static uint16_t Mask(int type, int rot);

private:
    bool CanPlace(int type, int rot, int px, int py) const;
    bool TryMove(int dx, int dy);
    bool TryRotate(int dir);
    void HardDrop();
    void HoldPiece();
    void SpawnFromQueue();
    void SpawnPiece(int type);
    void LockPiece();
    void FinishClear();
    void GameOver();
    void AppendBag();
    void SetMessage(const std::wstring& text);
    void UpdateHorizontal(double dt);
    double GravityInterval() const;

    ColorIndex board_[kRows][kCols]{};
    Piece piece_{};
    std::deque<int> queue_;
    std::mt19937 rng_;
    State state_ = State::Playing;
    int hold_ = 0;
    bool holdUsed_ = false;

    int score_ = 0;
    int best_ = 0;
    int level_ = 1;
    int lines_ = 0;
    int combo_ = -1;
    bool newBest_ = false;

    double dropAcc_ = 0.0;
    double lockAcc_ = 0.0;
    int lockResets_ = 0;
    double clearTimer_ = 0.0;
    double clearDuration_ = 110.0;
    std::vector<int> clearingRows_;

    bool leftHeld_ = false;
    bool rightHeld_ = false;
    bool softHeld_ = false;
    int lastDir_ = 0;
    double dasAcc_ = 0.0;
    double arrAcc_ = 0.0;

    std::wstring message_;
    double messageTimer_ = 0.0;
};

}  // namespace tetris
