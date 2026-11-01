#include "game.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace tetris {
namespace {

// Each piece is described by its four SRS rotation states in a 4x4 box.
// '#' marks a filled cell. States run 0 (spawn), R, 2, L.
const char* const kArt[kPieceCount][4][4] = {
    {  // I
        {"....", "####", "....", "...."},
        {"..#.", "..#.", "..#.", "..#."},
        {"....", "....", "####", "...."},
        {".#..", ".#..", ".#..", ".#.."},
    },
    {  // J
        {"#...", "###.", "....", "...."},
        {".##.", ".#..", ".#..", "...."},
        {"....", "###.", "..#.", "...."},
        {".#..", ".#..", "##..", "...."},
    },
    {  // L
        {"..#.", "###.", "....", "...."},
        {".#..", ".#..", ".##.", "...."},
        {"....", "###.", "#...", "...."},
        {"##..", ".#..", ".#..", "...."},
    },
    {  // O
        {"##..", "##..", "....", "...."},
        {"##..", "##..", "....", "...."},
        {"##..", "##..", "....", "...."},
        {"##..", "##..", "....", "...."},
    },
    {  // S
        {".##.", "##..", "....", "...."},
        {".#..", ".##.", "..#.", "...."},
        {"....", ".##.", "##..", "...."},
        {"#...", "##..", ".#..", "...."},
    },
    {  // T
        {".#..", "###.", "....", "...."},
        {".#..", ".##.", ".#..", "...."},
        {"....", "###.", ".#..", "...."},
        {".#..", "##..", ".#..", "...."},
    },
    {  // Z
        {"##..", ".##.", "....", "...."},
        {"..#.", ".##.", ".#..", "...."},
        {"....", "##..", ".##.", "...."},
        {".#..", "##..", "#...", "...."},
    },
};

uint16_t gMasks[kPieceCount][4]{};
bool gMasksReady = false;

void InitMasks() {
    if (gMasksReady) return;
    for (int t = 0; t < kPieceCount; ++t) {
        for (int r = 0; r < 4; ++r) {
            uint16_t m = 0;
            for (int row = 0; row < 4; ++row) {
                for (int col = 0; col < 4; ++col) {
                    if (kArt[t][r][row][col] == '#') {
                        m = static_cast<uint16_t>(m | (1u << (row * 4 + col)));
                    }
                }
            }
            gMasks[t][r] = m;
        }
    }
    gMasksReady = true;
}

// SRS wall kicks. Offsets are (x right, y up); the caller flips y.
const int kKickCW_JLSTZ[4][5][2] = {
    {{0, 0}, {-1, 0}, {-1, 1}, {0, -2}, {-1, -2}},
    {{0, 0}, {1, 0}, {1, -1}, {0, 2}, {1, 2}},
    {{0, 0}, {1, 0}, {1, 1}, {0, -2}, {1, -2}},
    {{0, 0}, {-1, 0}, {-1, -1}, {0, 2}, {-1, 2}},
};
const int kKickCCW_JLSTZ[4][5][2] = {
    {{0, 0}, {1, 0}, {1, 1}, {0, -2}, {1, -2}},
    {{0, 0}, {1, 0}, {1, -1}, {0, 2}, {1, 2}},
    {{0, 0}, {-1, 0}, {-1, 1}, {0, -2}, {-1, -2}},
    {{0, 0}, {-1, 0}, {-1, -1}, {0, 2}, {-1, 2}},
};
const int kKickCW_I[4][5][2] = {
    {{0, 0}, {-2, 0}, {1, 0}, {-2, -1}, {1, 2}},
    {{0, 0}, {-1, 0}, {2, 0}, {-1, 2}, {2, -1}},
    {{0, 0}, {2, 0}, {-1, 0}, {2, 1}, {-1, -2}},
    {{0, 0}, {1, 0}, {-2, 0}, {1, -2}, {-2, 1}},
};
const int kKickCCW_I[4][5][2] = {
    {{0, 0}, {-1, 0}, {2, 0}, {-1, 2}, {2, -1}},
    {{0, 0}, {2, 0}, {-1, 0}, {2, 1}, {-1, -2}},
    {{0, 0}, {1, 0}, {-2, 0}, {1, -2}, {-2, 1}},
    {{0, 0}, {-2, 0}, {1, 0}, {-2, -1}, {1, 2}},
};

const wchar_t* ClearName(int n) {
    switch (n) {
        case 1: return L"SINGLE";
        case 2: return L"DOUBLE";
        case 3: return L"TRIPLE";
        case 4: return L"TETRIS";
        default: return L"";
    }
}

}  // namespace

uint16_t Game::Mask(int type, int rot) {
    InitMasks();
    if (type < 1 || type > kPieceCount) return 0;
    return gMasks[type - 1][rot & 3];
}

Game::Game() {
    const auto seed = static_cast<uint32_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    rng_.seed(seed ^ std::random_device{}());
    Reset();
}

void Game::Reset() {
    std::memset(board_, 0, sizeof(board_));
    queue_.clear();
    piece_ = Piece{};
    hold_ = 0;
    holdUsed_ = false;
    score_ = 0;
    level_ = 1;
    lines_ = 0;
    combo_ = -1;
    newBest_ = false;
    dropAcc_ = lockAcc_ = 0.0;
    lockResets_ = 0;
    clearTimer_ = 0.0;
    clearingRows_.clear();
    leftHeld_ = rightHeld_ = softHeld_ = false;
    lastDir_ = 0;
    dasAcc_ = arrAcc_ = 0.0;
    message_.clear();
    messageTimer_ = 0.0;
    state_ = State::Playing;
    while (static_cast<int>(queue_.size()) < 7) AppendBag();
    SpawnFromQueue();
}

void Game::SetBest(int best) {
    best_ = std::max(0, best);
    if (score_ <= best_) newBest_ = false;
}

void Game::AppendBag() {
    int bag[kPieceCount] = {1, 2, 3, 4, 5, 6, 7};
    std::shuffle(std::begin(bag), std::end(bag), rng_);
    for (int type : bag) queue_.push_back(type);
}

void Game::SpawnFromQueue() {
    if (queue_.empty()) AppendBag();
    const int type = queue_.front();
    queue_.pop_front();
    SpawnPiece(type);
    while (static_cast<int>(queue_.size()) < 7) AppendBag();
}

void Game::SpawnPiece(int type) {
    piece_.type = type;
    piece_.rot = 0;
    piece_.y = 0;
    piece_.x = (type == 4) ? 4 : 3;  // O sits in columns 4-5, the rest in 3-5
    dropAcc_ = lockAcc_ = 0.0;
    lockResets_ = 0;
    holdUsed_ = false;
    dasAcc_ = arrAcc_ = 0.0;
    lastDir_ = 0;
    if (!CanPlace(piece_.type, piece_.rot, piece_.x, piece_.y)) {
        piece_.type = 0;
        GameOver();
    }
}

bool Game::CanPlace(int type, int rot, int px, int py) const {
    if (type < 1 || type > kPieceCount) return false;
    const uint16_t m = Mask(type, rot);
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            if (!(m & (1u << (r * 4 + c)))) continue;
            const int x = px + c;
            const int y = py + r;
            if (x < 0 || x >= kCols || y >= kRows) return false;
            if (y >= 0 && board_[y][x] != 0) return false;
        }
    }
    return true;
}

bool Game::TryMove(int dx, int dy) {
    if (!CanPlace(piece_.type, piece_.rot, piece_.x + dx, piece_.y + dy)) return false;
    piece_.x += dx;
    piece_.y += dy;
    if (!CanPlace(piece_.type, piece_.rot, piece_.x, piece_.y + 1) && lockResets_ < 15) {
        lockAcc_ = 0.0;
        ++lockResets_;
    }
    return true;
}

bool Game::TryRotate(int dir) {
    if (piece_.type == 4) return true;  // O never changes shape
    const int from = piece_.rot;
    const int to = (from + (dir > 0 ? 1 : 3)) & 3;
    const int (*table)[5][2] =
        (piece_.type == 1) ? (dir > 0 ? kKickCW_I : kKickCCW_I)
                           : (dir > 0 ? kKickCW_JLSTZ : kKickCCW_JLSTZ);
    for (int i = 0; i < 5; ++i) {
        const int nx = piece_.x + table[from][i][0];
        const int ny = piece_.y - table[from][i][1];
        if (!CanPlace(piece_.type, to, nx, ny)) continue;
        piece_.rot = to;
        piece_.x = nx;
        piece_.y = ny;
        if (!CanPlace(piece_.type, to, nx, ny + 1) && lockResets_ < 15) {
            lockAcc_ = 0.0;
            ++lockResets_;
        }
        return true;
    }
    return false;
}

void Game::Handle(Action action) {
    if (action == Action::Restart) {
        Reset();
        return;
    }
    if (action == Action::TogglePause) {
        TogglePause();
        return;
    }
    // Key releases must always clear held state, even while paused or game over.
    switch (action) {
        case Action::LeftUp:
            leftHeld_ = false;
            return;
        case Action::RightUp:
            rightHeld_ = false;
            return;
        case Action::SoftUp:
            softHeld_ = false;
            return;
        default:
            break;
    }
    if (state_ != State::Playing) return;

    switch (action) {
        case Action::LeftDown:
            leftHeld_ = true;
            break;
        case Action::RightDown:
            rightHeld_ = true;
            break;
        case Action::SoftDown:
            softHeld_ = true;
            if (TryMove(0, 1)) {
                ++score_;
                dropAcc_ = 0.0;
            }
            return;
        case Action::RotateCW:
            TryRotate(1);
            return;
        case Action::RotateCCW:
            TryRotate(-1);
            return;
        case Action::HardDrop:
            HardDrop();
            return;
        case Action::Hold:
            HoldPiece();
            return;
        default:
            return;
    }

    // A fresh left/right press moves immediately and restarts auto-repeat.
    const int want = (rightHeld_ ? 1 : 0) - (leftHeld_ ? 1 : 0);
    if (want != 0) {
        lastDir_ = want;
        TryMove(want, 0);
        dasAcc_ = 0.0;
        arrAcc_ = 0.0;
    } else {
        lastDir_ = 0;
    }
}

void Game::UpdateHorizontal(double dt) {
    const int want = (rightHeld_ ? 1 : 0) - (leftHeld_ ? 1 : 0);
    if (want == 0) {
        lastDir_ = 0;
        dasAcc_ = arrAcc_ = 0.0;
        return;
    }
    if (want != lastDir_) {
        lastDir_ = want;
        TryMove(want, 0);
        dasAcc_ = arrAcc_ = 0.0;
        return;
    }
    dasAcc_ += dt;
    if (dasAcc_ < 150.0) return;
    arrAcc_ += dt;
    while (arrAcc_ >= 33.0) {
        arrAcc_ -= 33.0;
        if (!TryMove(want, 0)) {
            arrAcc_ = 0.0;
            break;
        }
    }
}

double Game::GravityInterval() const {
    const double base = 900.0 * std::pow(0.82, level_ - 1);
    return std::max(55.0, base);
}

void Game::Update(double dtMs) {
    if (messageTimer_ > 0.0) messageTimer_ = std::max(0.0, messageTimer_ - dtMs);

    if (state_ == State::Clearing) {
        clearTimer_ -= dtMs;
        if (clearTimer_ <= 0.0) FinishClear();
        return;
    }
    if (state_ != State::Playing || piece_.type == 0) return;

    UpdateHorizontal(dtMs);

    double interval = GravityInterval();
    if (softHeld_) interval = std::min(interval, 34.0);
    dropAcc_ += dtMs;
    while (dropAcc_ >= interval) {
        dropAcc_ -= interval;
        if (!CanPlace(piece_.type, piece_.rot, piece_.x, piece_.y + 1)) {
            dropAcc_ = 0.0;
            break;
        }
        ++piece_.y;
        lockAcc_ = 0.0;
        if (softHeld_) ++score_;
    }

    if (!CanPlace(piece_.type, piece_.rot, piece_.x, piece_.y + 1)) {
        lockAcc_ += dtMs;
        if (lockAcc_ >= 500.0) LockPiece();
    } else {
        lockAcc_ = 0.0;
    }
}

void Game::HardDrop() {
    int cells = 0;
    while (CanPlace(piece_.type, piece_.rot, piece_.x, piece_.y + 1)) {
        ++piece_.y;
        ++cells;
    }
    score_ += 2 * cells;
    LockPiece();
}

void Game::HoldPiece() {
    if (holdUsed_) return;
    const int current = piece_.type;
    if (hold_ == 0) {
        hold_ = current;
        SpawnFromQueue();
    } else {
        const int swap = hold_;
        hold_ = current;
        SpawnPiece(swap);
    }
    holdUsed_ = true;
}

void Game::LockPiece() {
    if (piece_.type == 0) return;
    const uint16_t m = Mask(piece_.type, piece_.rot);
    bool lockedAboveTop = false;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            if (!(m & (1u << (r * 4 + c)))) continue;
            const int y = piece_.y + r;
            if (y < 0) {
                lockedAboveTop = true;
            } else {
                board_[y][piece_.x + c] = static_cast<ColorIndex>(piece_.type);
            }
        }
    }
    piece_.type = 0;

    if (lockedAboveTop) {
        GameOver();
        return;
    }

    std::vector<int> full;
    for (int r = 0; r < kRows; ++r) {
        bool complete = true;
        for (int c = 0; c < kCols; ++c) {
            if (board_[r][c] == 0) {
                complete = false;
                break;
            }
        }
        if (complete) full.push_back(r);
    }

    if (full.empty()) {
        combo_ = -1;
        SpawnFromQueue();
        return;
    }

    const int n = std::min(4, static_cast<int>(full.size()));
    const int previousLevel = level_;
    static const int kBaseScore[5] = {0, 100, 300, 500, 800};
    score_ += kBaseScore[n] * level_;
    lines_ += n;
    level_ = std::min(kMaxLevel, 1 + lines_ / 10);
    combo_ = combo_ + 1;  // starts at -1, so the first clear is combo 0
    if (combo_ > 0) score_ += 50 * combo_ * level_;

    clearingRows_ = std::move(full);
    clearTimer_ = clearDuration_;
    state_ = State::Clearing;

    std::wstring msg = ClearName(n);
    if (combo_ > 0) {
        msg += L"  COMBO x";
        msg += std::to_wstring(combo_ + 1);
    }
    if (level_ > previousLevel) {
        msg += L"  LEVEL ";
        msg += std::to_wstring(level_);
    }
    SetMessage(msg);
}

void Game::FinishClear() {
    bool cleared[kRows] = {};
    for (int row : clearingRows_) {
        if (row >= 0 && row < kRows) cleared[row] = true;
    }
    int dst = kRows - 1;
    for (int src = kRows - 1; src >= 0; --src) {
        if (cleared[src]) continue;
        std::memcpy(board_[dst], board_[src], sizeof(board_[0]));
        --dst;
    }
    for (; dst >= 0; --dst) std::memset(board_[dst], 0, sizeof(board_[0]));

    clearingRows_.clear();
    clearTimer_ = 0.0;
    state_ = State::Playing;
    SpawnFromQueue();
}

void Game::GameOver() {
    state_ = State::GameOver;
    message_.clear();
    messageTimer_ = 0.0;
    leftHeld_ = rightHeld_ = softHeld_ = false;
    if (score_ > best_) {
        best_ = score_;
        newBest_ = true;
    } else {
        newBest_ = false;
    }
}

void Game::Pause() {
    if (state_ != State::Playing) return;
    state_ = State::Paused;
    leftHeld_ = rightHeld_ = softHeld_ = false;
    lastDir_ = 0;
    dasAcc_ = arrAcc_ = 0.0;
}

void Game::TogglePause() {
    if (state_ == State::Playing) {
        Pause();
    } else if (state_ == State::Paused) {
        state_ = State::Playing;
    }
}

int Game::GhostY() const {
    if (piece_.type == 0) return piece_.y;
    int y = piece_.y;
    while (CanPlace(piece_.type, piece_.rot, piece_.x, y + 1)) ++y;
    return y;
}

void Game::SetMessage(const std::wstring& text) {
    message_ = text;
    messageTimer_ = 1000.0;
}

double Game::messageAlpha() const {
    if (messageTimer_ <= 0.0) return 0.0;
    return std::clamp(messageTimer_ / 300.0, 0.0, 1.0);
}

}  // namespace tetris
