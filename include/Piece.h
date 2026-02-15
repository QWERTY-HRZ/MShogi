#ifndef PIECE_H
#define PIECE_H

#endif // PIECE_H

#pragma once
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
    Piece(Player owner, PieceType type) : m_owner(owner), m_type(type) {}
    virtual ~Piece() = default;

    Player getOwner() const { return m_owner; }
    PieceType getType() const { return m_type; }
    virtual std::string getName() const = 0;
    // 手鞠管理
    void setTurnsInHand(int v) { m_turnsInHand = v; }
    void incrementTurnsInHand() { m_turnsInHand++; }
    void decrementTurnsInHand() { if (m_turnsInHand > 0) m_turnsInHand--; } // Undo用
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
