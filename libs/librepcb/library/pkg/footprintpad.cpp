/*
 * LibrePCB - Professional EDA for everyone!
 * Copyright (C) 2013 LibrePCB Developers, see AUTHORS.md for contributors.
 * http://librepcb.org/
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*****************************************************************************************
 *  Includes
 ****************************************************************************************/
#include <QtCore>
#include <librepcb/common/graphics/graphicslayer.h>
#include "footprintpad.h"
#include "footprintpadgraphicsitem.h"

/*****************************************************************************************
 *  Namespace
 ****************************************************************************************/
namespace librepcb {
namespace library {

/*****************************************************************************************
 *  Constructors / Destructor
 ****************************************************************************************/

FootprintPad::FootprintPad(const FootprintPad& other) noexcept :
    mRegisteredGraphicsItem(nullptr)
{
    *this = other; // use assignment operator
}

FootprintPad::FootprintPad(const Uuid& padUuid, const Point& pos, const Angle& rot,
        Shape shape, const Length& width, const Length& height,
        const Length& drillDiameter, BoardSide side) noexcept :
    mPackagePadUuid(padUuid), mPosition(pos), mRotation(rot), mShape(shape), mWidth(width),
    mHeight(height), mDrillDiameter(drillDiameter), mBoardSide(side),
    mRegisteredGraphicsItem(nullptr)
{
}

FootprintPad::FootprintPad(const SExpression& node) :
    mRegisteredGraphicsItem(nullptr)
{
    // read attributes
    mPackagePadUuid = node.getChildByIndex(0).getValue<Uuid>(true);
    mPosition = Point(node.getChildByPath("pos"));
    mRotation = node.getValueByPath<Angle>("rot", true);
    mBoardSide = stringToBoardSide(node.getValueByPath<QString>("side", true));
    mShape = stringToShape(node.getValueByPath<QString>("shape", true));
    mDrillDiameter = node.getValueByPath<Length>("drill", true);
    mWidth = Point(node.getChildByPath("size")).getX();
    mHeight = Point(node.getChildByPath("size")).getY();

    if (!checkAttributesValidity()) throw LogicError(__FILE__, __LINE__);
}

FootprintPad::~FootprintPad() noexcept
{
    Q_ASSERT(mRegisteredGraphicsItem == nullptr);
}

/*****************************************************************************************
 *  Getters
 ****************************************************************************************/

QString FootprintPad::getLayerName() const noexcept
{
    switch (mBoardSide) {
        case BoardSide::TOP:        return GraphicsLayer::sTopCopper;
        case BoardSide::BOTTOM:     return GraphicsLayer::sBotCopper;
        case BoardSide::THT:        return GraphicsLayer::sBoardPadsTht;
        default: Q_ASSERT(false);   return "";
    }
}

bool FootprintPad::isOnLayer(const QString& name) const noexcept
{
    if (mBoardSide == BoardSide::THT) {
        return GraphicsLayer::isCopperLayer(name);
    } else {
        return (name == getLayerName());
    }
}

Path FootprintPad::getOutline(const Length& expansion) const noexcept
{
    Length width = mWidth + (expansion * 2);
    Length height = mHeight + (expansion * 2);
    if (width > 0 && height > 0) {
        switch (mShape) {
            case Shape::ROUND:      return Path::obround(width, height);
            case Shape::RECT:       return Path::centeredRect(width, height);
            case Shape::OCTAGON:    return Path::octagon(width, height);
            default:                Q_ASSERT(false); break;
        }
    }
    return Path();
}

QPainterPath FootprintPad::toQPainterPathPx(const Length& expansion) const noexcept
{
    QPainterPath p = getOutline(expansion).toQPainterPathPx();
    if (mBoardSide == BoardSide::THT) {
        p.setFillRule(Qt::OddEvenFill); // important to subtract the hole!
        p.addEllipse(QPointF(0, 0), mDrillDiameter.toPx()/2, mDrillDiameter.toPx()/2);
    }
    return p;
}

/*****************************************************************************************
 *  Setters
 ****************************************************************************************/
void FootprintPad::setPosition(const Point& pos) noexcept
{
    mPosition = pos;
    if (mRegisteredGraphicsItem) mRegisteredGraphicsItem->setPosition(mPosition);
}

void FootprintPad::setPackagePadUuid(const Uuid& pad) noexcept
{
    mPackagePadUuid = pad;
}

void FootprintPad::setRotation(const Angle& rot) noexcept
{
    mRotation = rot;
    if (mRegisteredGraphicsItem) mRegisteredGraphicsItem->setRotation(mRotation);
}

void FootprintPad::setShape(Shape shape) noexcept
{
    mShape = shape;
    if (mRegisteredGraphicsItem) mRegisteredGraphicsItem->setShape(toQPainterPathPx());
}

void FootprintPad::setWidth(const Length& width) noexcept
{
    mWidth = width;
    if (mRegisteredGraphicsItem) mRegisteredGraphicsItem->setShape(toQPainterPathPx());
}

void FootprintPad::setHeight(const Length& height) noexcept
{
    mHeight = height;
    if (mRegisteredGraphicsItem) mRegisteredGraphicsItem->setShape(toQPainterPathPx());
}

void FootprintPad::setDrillDiameter(const Length& diameter) noexcept
{
    mDrillDiameter = diameter;
    if (mRegisteredGraphicsItem) mRegisteredGraphicsItem->setShape(toQPainterPathPx());
}

void FootprintPad::setBoardSide(BoardSide side) noexcept
{
    mBoardSide = side;
    if (mRegisteredGraphicsItem) mRegisteredGraphicsItem->setLayerName(getLayerName());
    if (mRegisteredGraphicsItem) mRegisteredGraphicsItem->setShape(toQPainterPathPx());
}

/*****************************************************************************************
 *  General Methods
 ****************************************************************************************/

void FootprintPad::registerGraphicsItem(FootprintPadGraphicsItem& item) noexcept
{
    Q_ASSERT(!mRegisteredGraphicsItem);
    mRegisteredGraphicsItem = &item;
}

void FootprintPad::unregisterGraphicsItem(FootprintPadGraphicsItem& item) noexcept
{
    Q_ASSERT(mRegisteredGraphicsItem == &item);
    mRegisteredGraphicsItem = nullptr;
}

void FootprintPad::serialize(SExpression& root) const
{
    if (!checkAttributesValidity()) throw LogicError(__FILE__, __LINE__);

    root.appendToken(mPackagePadUuid);
    root.appendTokenChild("side", boardSideToString(mBoardSide), false);
    root.appendTokenChild("shape", shapeToString(mShape), false);
    root.appendChild(mPosition.serializeToDomElement("pos"), true);
    root.appendTokenChild("rot", mRotation, false);
    root.appendChild(Point(mWidth, mHeight).serializeToDomElement("size"), false);
    root.appendTokenChild("drill", mDrillDiameter, false);
}

/*****************************************************************************************
 *  Operator Overloadings
 ****************************************************************************************/

bool FootprintPad::operator==(const FootprintPad& rhs) const noexcept
{
    if (mPackagePadUuid != rhs.mPackagePadUuid) return false;
    if (mPosition != rhs.mPosition) return false;
    if (mRotation != rhs.mRotation) return false;
    if (mShape != rhs.mShape) return false;
    if (mWidth != rhs.mWidth) return false;
    if (mHeight != rhs.mHeight) return false;
    if (mDrillDiameter != rhs.mDrillDiameter) return false;
    if (mBoardSide != rhs.mBoardSide) return false;
    return true;
}

FootprintPad& FootprintPad::operator=(const FootprintPad& rhs) noexcept
{
    mPackagePadUuid = rhs.mPackagePadUuid;
    mPosition = rhs.mPosition;
    mRotation = rhs.mRotation;
    mShape = rhs.mShape;
    mWidth = rhs.mWidth;
    mHeight = rhs.mHeight;
    mDrillDiameter = rhs.mDrillDiameter;
    mBoardSide = rhs.mBoardSide;
    return *this;
}

/*****************************************************************************************
 *  Private Methods
 ****************************************************************************************/

bool FootprintPad::checkAttributesValidity() const noexcept
{
    if (mPackagePadUuid.isNull())                             return false;
    if (mWidth <= 0)                                return false;
    if (mHeight <= 0)                               return false;
    if (mDrillDiameter < 0)                         return false;
    return true;
}

/*****************************************************************************************
 *  Static Methods
 ****************************************************************************************/

FootprintPad::Shape FootprintPad::stringToShape(const QString& shape)
{
    if      (shape == QLatin1String("round"))   return Shape::ROUND;
    else if (shape == QLatin1String("rect"))    return Shape::RECT;
    else if (shape == QLatin1String("octagon")) return Shape::OCTAGON;
    else throw RuntimeError(__FILE__, __LINE__, shape);
}

QString FootprintPad::shapeToString(Shape shape) noexcept
{
    switch (shape)
    {
        case Shape::ROUND:    return QString("round");
        case Shape::RECT:     return QString("rect");
        case Shape::OCTAGON:  return QString("octagon");
        default: Q_ASSERT(false); return QString();
    }
}

FootprintPad::BoardSide FootprintPad::stringToBoardSide(const QString& side)
{
    if      (side == QLatin1String("top"))      return BoardSide::TOP;
    else if (side == QLatin1String("bottom"))   return BoardSide::BOTTOM;
    else if (side == QLatin1String("tht"))      return BoardSide::THT;
    else throw RuntimeError(__FILE__, __LINE__, side);
}

QString FootprintPad::boardSideToString(BoardSide side) noexcept
{
    switch (side)
    {
        case BoardSide::TOP:    return QString("top");
        case BoardSide::BOTTOM: return QString("bottom");
        case BoardSide::THT:    return QString("tht");
        default: Q_ASSERT(false); return QString();
    }
}

/*****************************************************************************************
 *  End of File
 ****************************************************************************************/

} // namespace library
} // namespace librepcb
