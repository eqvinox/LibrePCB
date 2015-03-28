/*
 * EDA4U - Professional EDA for everyone!
 * Copyright (C) 2013 Urban Bruhin
 * http://eda4u.ubruhin.ch/
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
#include "ses_addcomponents.h"
#include "../schematiceditor.h"
#include "ui_schematiceditor.h"
#include "../../project.h"
#include "../../library/projectlibrary.h"
#include "../../../library/genericcomponent.h"
#include "../../circuit/cmd/cmdgencompinstadd.h"
#include "../../../common/undostack.h"
#include "../cmd/cmdsymbolinstanceadd.h"
#include "../../circuit/gencompinstance.h"
#include "../cmd/cmdsymbolinstanceedit.h"
#include "../symbolinstance.h"
#include "../schematic.h"
#include "../../dialogs/addgencompdialog.h"

namespace project {

/*****************************************************************************************
 *  Constructors / Destructor
 ****************************************************************************************/

SES_AddComponents::SES_AddComponents(SchematicEditor& editor, Ui::SchematicEditor& editorUi) :
    SES_Base(editor, editorUi),
    mIsUndoCmdActive(false), mAddGenCompDialog(nullptr), mLastAngle(0),
    mGenComp(nullptr), mGenCompSymbVar(nullptr), mCurrentSymbVarItem(nullptr),
    mCurrentSymbolToPlace(nullptr), mCurrentSymbolEditCommand(nullptr)
{
}

SES_AddComponents::~SES_AddComponents()
{
}

/*****************************************************************************************
 *  General Methods
 ****************************************************************************************/

SES_Base::ProcRetVal SES_AddComponents::process(SEE_Base* event) noexcept
{
    switch (event->getType())
    {
        case SEE_Base::AbortCommand:
        {
            if (mAddGenCompDialog)
            {
                try
                {
                    if (!abortCommand(true)) return PassToParentState;
                    mLastAngle.setAngleMicroDeg(0); // reset the angle
                    startAddingComponent();
                    return ForceStayInState;
                }
                catch (UserCanceled& exc)
                {
                }
                catch (Exception& exc)
                {
                    QMessageBox::critical(&mEditor, tr("Error"), exc.getUserMsg());
                }
            }
            return PassToParentState;
        }
        case SEE_Base::StartAddComponent:
        {
            try
            {
                // start adding (another) component
                SEE_StartAddComponent* e = dynamic_cast<SEE_StartAddComponent*>(event);
                Q_ASSERT(e);
                if (!abortCommand(true)) return PassToParentState;
                mLastAngle.setAngleMicroDeg(0); // reset the angle
                startAddingComponent(e->getGenCompUuid(), e->getSymbVarUuid());
                return ForceStayInState;
            }
            catch (UserCanceled& exc)
            {
            }
            catch (Exception& exc)
            {
                QMessageBox::critical(&mEditor, tr("Error"), exc.getUserMsg());
            }
            return PassToParentState;
        }
        case SEE_Base::Edit_RotateCW:
            mCurrentSymbolEditCommand->rotate(Angle::deg90(), mCurrentSymbolToPlace->getPosition(), true);
            return ForceStayInState;
        case SEE_Base::Edit_RotateCCW:
            mCurrentSymbolEditCommand->rotate(-Angle::deg90(), mCurrentSymbolToPlace->getPosition(), true);
            return ForceStayInState;
        case SEE_Base::SchematicSceneEvent:
            return processSceneEvent(event);
        default:
            return PassToParentState;
    }
}

bool SES_AddComponents::entry(SEE_Base* event) noexcept
{
    // only accept events of type SEE_StartAddComponent
    if (!event) return false;
    if (event->getType() != SEE_Base::StartAddComponent) return false;
    SEE_StartAddComponent* e = dynamic_cast<SEE_StartAddComponent*>(event);
    Q_ASSERT(e); if (!e) return false;
    Q_ASSERT(mIsUndoCmdActive == false);
    mLastAngle.setAngleMicroDeg(0);

    // start adding the specified component
    try
    {
        startAddingComponent(e->getGenCompUuid(), e->getSymbVarUuid());
    }
    catch (UserCanceled& exc)
    {
        if (mIsUndoCmdActive) abortCommand(false);
        delete mAddGenCompDialog; mAddGenCompDialog = nullptr;
        return false;
    }
    catch (Exception& exc)
    {
        QMessageBox::critical(&mEditor, tr("Error"), QString(tr("Could not add component:\n\n%1")).arg(exc.getUserMsg()));
        if (mIsUndoCmdActive) abortCommand(false);
        delete mAddGenCompDialog; mAddGenCompDialog = nullptr;
        return false;
    }

    // update the command toolbar action
    mEditorUi.actionToolAddComponent->setCheckable(true);
    mEditorUi.actionToolAddComponent->setChecked(true);
    return true;
}

bool SES_AddComponents::exit(SEE_Base* event) noexcept
{
    Q_UNUSED(event);
    if (!abortCommand(true)) return false;
    Q_ASSERT(mIsUndoCmdActive == false);
    delete mAddGenCompDialog;   mAddGenCompDialog = nullptr;
    mEditorUi.actionToolAddComponent->setCheckable(false);
    mEditorUi.actionToolAddComponent->setChecked(false);
    return true;
}

/*****************************************************************************************
 *  Private Methods
 ****************************************************************************************/

SES_Base::ProcRetVal SES_AddComponents::processSceneEvent(SEE_Base* event) noexcept
{
    QEvent* qevent = SEE_RedirectedQEvent::getQEventFromSEE(event);
    Q_ASSERT(qevent); if (!qevent) return PassToParentState;
    Schematic* schematic = mEditor.getActiveSchematic();
    Q_ASSERT(schematic); if (!schematic) return PassToParentState;
    if (!mIsUndoCmdActive) return PassToParentState; // temporary

    switch (qevent->type())
    {
        case QEvent::GraphicsSceneMouseMove:
        {
            QGraphicsSceneMouseEvent* sceneEvent = dynamic_cast<QGraphicsSceneMouseEvent*>(qevent);
            Q_ASSERT(sceneEvent);
            Point pos = Point::fromPx(sceneEvent->scenePos(), mEditorUi.graphicsView->getGridInterval());
            // set temporary position of the current symbol
            Q_ASSERT(mCurrentSymbolEditCommand);
            mCurrentSymbolEditCommand->setPosition(pos, true);
            break;
        }

        case QEvent::GraphicsSceneMouseDoubleClick:
        case QEvent::GraphicsSceneMousePress:
        {
            QGraphicsSceneMouseEvent* sceneEvent = dynamic_cast<QGraphicsSceneMouseEvent*>(qevent);
            Q_ASSERT(sceneEvent);
            Point pos = Point::fromPx(sceneEvent->scenePos(), mEditorUi.graphicsView->getGridInterval());
            switch (sceneEvent->button())
            {
                case Qt::LeftButton:
                {
                    try
                    {
                        // place the current symbol finally
                        mCurrentSymbolEditCommand->setPosition(pos, false);
                        mProject.getUndoStack().appendToCommand(mCurrentSymbolEditCommand);
                        mCurrentSymbolEditCommand = nullptr;
                        mProject.getUndoStack().endCommand();
                        mIsUndoCmdActive = false;
                        mProject.getUndoStack().beginCommand(tr("Add Symbol to Schematic"));
                        mIsUndoCmdActive = true;

                        // check if there is a next symbol to add
                        mCurrentSymbVarItem = mGenCompSymbVar->getNextItem(mCurrentSymbVarItem);

                        if (mCurrentSymbVarItem)
                        {
                            // create the next symbol instance and add it to the schematic
                            CmdSymbolInstanceAdd* cmd = new CmdSymbolInstanceAdd(*schematic,
                                mCurrentSymbolToPlace->getGenCompInstance(),
                                mCurrentSymbVarItem->getUuid(), pos);
                            mProject.getUndoStack().appendToCommand(cmd);
                            mCurrentSymbolToPlace = cmd->getSymbolInstance();
                            Q_ASSERT(mCurrentSymbolToPlace);

                            // add command to move the current symbol
                            Q_ASSERT(mCurrentSymbolEditCommand == nullptr);
                            mCurrentSymbolEditCommand = new CmdSymbolInstanceEdit(*mCurrentSymbolToPlace);
                            mCurrentSymbolEditCommand->setRotation(mLastAngle, true);
                            return ForceStayInState;
                        }
                        else
                        {
                            // all symbols placed, start adding the next component
                            QUuid genCompUuid = mGenComp->getUuid();
                            QUuid symbVarUuid = mGenCompSymbVar->getUuid();
                            mProject.getUndoStack().endCommand();
                            mIsUndoCmdActive = false;
                            abortCommand(false); // reset attributes
                            startAddingComponent(genCompUuid, symbVarUuid);
                            return ForceStayInState;
                        }
                    }
                    catch (Exception& e)
                    {
                        QMessageBox::critical(&mEditor, tr("Error"), e.getUserMsg());
                        abortCommand(false);
                        return ForceLeaveState;
                    }
                    break;
                }

                case Qt::RightButton:
                    // rotate symbol
                    mLastAngle -= Angle::deg90();
                    mCurrentSymbolEditCommand->setRotation(mLastAngle, true);
                    return ForceStayInState;

                default:
                    break;
            }
            break;
        }

        default:
        {
            // Always accept graphics scene events, even if we do not react on some of the events!
            // This will give us the full control over the graphics scene. Otherwise, the graphics
            // scene can react on some events and disturb our state machine. Only the wheel event
            // is ignored because otherwise the view will not allow to zoom with the mouse wheel.
            if (qevent->type() != QEvent::GraphicsSceneWheel)
                return ForceStayInState;
            else
                return PassToParentState;
        }
    }

    return PassToParentState;
}

void SES_AddComponents::startAddingComponent(const QUuid& genComp,
                                             const QUuid& symbVar) throw (Exception)
{
    Schematic* schematic = mEditor.getActiveSchematic();
    Q_ASSERT(schematic); if (!schematic) throw LogicError(__FILE__, __LINE__);

    // get the scene position where the new symbol should be placed
    QPoint cursorPos = mEditorUi.graphicsView->mapFromGlobal(QCursor::pos());
    QPoint boundedCursorPos = QPoint(qBound(0, cursorPos.x(), mEditorUi.graphicsView->width()),
                                     qBound(0, cursorPos.y(), mEditorUi.graphicsView->height()));
    Point pos = Point::fromPx(mEditorUi.graphicsView->mapToScene(boundedCursorPos),
                              mEditorUi.graphicsView->getGridInterval());

    if (genComp.isNull() || symbVar.isNull())
    {
        // show generic component chooser dialog
        if (!mAddGenCompDialog)
            mAddGenCompDialog = new AddGenCompDialog(mProject, &mEditor);
        if (mAddGenCompDialog->exec() != QDialog::Accepted)
            throw UserCanceled(__FILE__, __LINE__); // abort

        // open the XML file
        library::GenericComponent* genComp = new library::GenericComponent(mAddGenCompDialog->getSelectedGenCompFilePath());
        QUuid genCompUuid = genComp->getUuid();
        Version genCompVersion = genComp->getVersion();
        delete genComp;

        // search the generic component in the library
        mGenComp = mProject.getLibrary().getGenComp(genCompUuid);
        if (mGenComp)
        {
            if (mGenComp->getVersion() != genCompVersion)
            {
                QMessageBox::information(&mEditor, tr("Different Version"),
                    QString(tr("The same generic component exists already in this "
                    "project's library but the version is different. The version %1 from "
                    "the project's library will be used instead of the version %2."))
                    .arg(mGenComp->getVersion().toStr(), genCompVersion.toStr()));
            }
        }
        else
        {
            // copy the generic component to the project library
            throw RuntimeError(__FILE__, __LINE__, QString(), tr("not yet implemented"));
        }
        mGenCompSymbVar = mGenComp->getSymbolVariantByUuid(mAddGenCompDialog->getSelectedSymbVarUuid());
    }
    else
    {
        // search the generic component in the library
        mGenComp = mProject.getLibrary().getGenComp(genComp);
        mGenCompSymbVar = mGenComp->getSymbolVariantByUuid(symbVar);
    }

    // check pointers
    if (!mGenComp)
    {
        throw LogicError(__FILE__, __LINE__, QString(),
            QString(tr("The generic component \"%1\" was not found in the "
            "project's library.")).arg(genComp.toString()));
    }
    if (!mGenCompSymbVar)
    {
        throw LogicError(__FILE__, __LINE__, QString(), QString(
            tr("Invalid symbol variant: \"%1\"")).arg(symbVar.toString()));
    }

    // start a new command
    mProject.getUndoStack().beginCommand(tr("Add Generic Component to Schematic"));
    mIsUndoCmdActive = true;

    // create a new generic component instance and add it to the circuit
    CmdGenCompInstAdd* cmd = new CmdGenCompInstAdd(mCircuit, *mGenComp,
                                                           *mGenCompSymbVar);
    mProject.getUndoStack().appendToCommand(cmd);

    // create the first symbol instance and add it to the schematic
    mCurrentSymbVarItem = mGenCompSymbVar->getItems().first();
    if (!mCurrentSymbVarItem)
    {
        throw RuntimeError(__FILE__, __LINE__, symbVar.toString(),
            QString(tr("The generic component with the UUID \"%1\" does not have "
                       "any symbol.")).arg(genComp.toString()));
    }
    CmdSymbolInstanceAdd* cmd2 = new CmdSymbolInstanceAdd(*schematic,
        *(cmd->getGenCompInstance()), mCurrentSymbVarItem->getUuid(), pos);
    mProject.getUndoStack().appendToCommand(cmd2);
    mCurrentSymbolToPlace = cmd2->getSymbolInstance();
    Q_ASSERT(mCurrentSymbolToPlace);

    // add command to move the current symbol
    Q_ASSERT(mCurrentSymbolEditCommand == nullptr);
    mCurrentSymbolEditCommand = new CmdSymbolInstanceEdit(*mCurrentSymbolToPlace);
    mCurrentSymbolEditCommand->setRotation(mLastAngle, true);
}

bool SES_AddComponents::abortCommand(bool showErrMsgBox) noexcept
{
    try
    {
        // delete the current move command
        delete mCurrentSymbolEditCommand;
        mCurrentSymbolEditCommand = nullptr;

        // abort the undo command
        if (mIsUndoCmdActive)
        {
            mProject.getUndoStack().abortCommand();
            mIsUndoCmdActive = false;
        }

        // reset attributes, go back to idle state
        mGenComp = nullptr;
        mGenCompSymbVar = nullptr;
        mCurrentSymbVarItem = nullptr;
        mCurrentSymbolToPlace = nullptr;
        return true;
    }
    catch (Exception& e)
    {
        if (showErrMsgBox) QMessageBox::critical(&mEditor, tr("Error"), e.getUserMsg());
        return false;
    }
}

/*****************************************************************************************
 *  End of File
 ****************************************************************************************/

} // namespace project
