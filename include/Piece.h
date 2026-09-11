#pragma once
#include <memory>
#include <string>

enum class Player {
    Sente,
    Gote
};

enum class PieceType {
    King,
    Rook,
    Bishop,
    Pawn,
    Hou
};

class Piece {
public:
    static constexpr int HAND_READY_TURNS = 4;

    Piece(Player owner, PieceType type) : m_owner(owner), m_type(type) {}
    virtual ~Piece() = default;

    Player getOwner() const { return m_owner; }
    PieceType getType() const { return m_type; }
    virtual std::string getName() const = 0;
    // 禁手状态封顶为 4，避免已解禁手驹产生无限多个等价状态。
    void setTurnsInHand(int v) {
        m_turnsInHand = v < 0 ? 0 : (v > HAND_READY_TURNS ? HAND_READY_TURNS : v);
    }
    void advanceTurnInHand() {
        if (m_turnsInHand < HAND_READY_TURNS) ++m_turnsInHand;
    }
    int getTurnsInHand() const { return m_turnsInHand; }

protected:
    Player m_owner;
    PieceType m_type;
    int m_turnsInHand = 0;
};

class King : public Piece {
public:
    explicit King(Player owner) : Piece(owner, PieceType::King) {}
    std::string getName() const override { return "King"; }
};

class Rook : public Piece {
public:
    explicit Rook(Player owner) : Piece(owner, PieceType::Rook) {}
    std::string getName() const override { return "Rook"; }
};

class Bishop : public Piece {
public:
    explicit Bishop(Player owner) : Piece(owner, PieceType::Bishop) {}
    std::string getName() const override { return "Bishop"; }
};

class Pawn : public Piece {
public:
    explicit Pawn(Player owner) : Piece(owner, PieceType::Pawn) {}
    std::string getName() const override { return "Pawn"; }
};

class Hou : public Piece {
public:
    explicit Hou(Player owner) : Piece(owner, PieceType::Hou) {}
    std::string getName() const override { return "Hou"; }
};

inline std::shared_ptr<Piece> makePiece(PieceType type, Player owner) {
    switch (type) {
        case PieceType::King: return std::make_shared<King>(owner);
        case PieceType::Rook: return std::make_shared<Rook>(owner);
        case PieceType::Bishop: return std::make_shared<Bishop>(owner);
        case PieceType::Pawn: return std::make_shared<Pawn>(owner);
        case PieceType::Hou: return std::make_shared<Hou>(owner);
    }
    return nullptr;
}
