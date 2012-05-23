/*
  ==============================================================================

   This file is part of the JUCE library - "Jules' Utility Class Extensions"
   Copyright 2004-11 by Raw Material Software Ltd.

  ------------------------------------------------------------------------------

   JUCE can be redistributed and/or modified under the terms of the GNU General
   Public License (Version 2), as published by the Free Software Foundation.
   A copy of the license is included in the JUCE distribution, or can be found
   online at www.gnu.org/licenses.

   JUCE is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
   A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

  ------------------------------------------------------------------------------

   To release a closed-source product which uses JUCE, commercial licenses are
   available: visit www.rawmaterialsoftware.com/juce for more information.

  ==============================================================================
*/

bool juce_performDragDropFiles (const StringArray&, const bool copyFiles, bool& shouldStop);
bool juce_performDragDropText (const String&, bool& shouldStop);


//==============================================================================
class DragImageComponent  : public Component,
                            public Timer
{
public:
    DragImageComponent (const Image& im,
                        const var& desc,
                        Component* const sourceComponent,
                        Component* const mouseDragSource_,
                        DragAndDropContainer& owner_,
                        const Point<int>& imageOffset_)
        : sourceDetails (desc, sourceComponent, Point<int>()),
          image (im),
          owner (owner_),
          mouseDragSource (mouseDragSource_),
          imageOffset (imageOffset_),
          hasCheckedForExternalDrag (false),
          isDoingExternalDrag (false)
    {
        setSize (im.getWidth(), im.getHeight());

        if (mouseDragSource == nullptr)
            mouseDragSource = sourceComponent;

        mouseDragSource->addMouseListener (this, false);

        startTimer (200);

        setInterceptsMouseClicks (false, false);
        setAlwaysOnTop (true);
    }

    ~DragImageComponent()
    {
        if (owner.dragImageComponent == this)
            owner.dragImageComponent.release();

        if (mouseDragSource != nullptr)
        {
            mouseDragSource->removeMouseListener (this);

            DragAndDropTarget* const current = getCurrentlyOver();

            if (current != nullptr && current->isInterestedInDragSource (sourceDetails))
                current->itemDragExit (sourceDetails);
        }
    }

    void paint (Graphics& g)
    {
        if (isOpaque())
            g.fillAll (Colours::white);

        g.setOpacity (1.0f);
        g.drawImageAt (image, 0, 0);
    }

    void mouseUp (const MouseEvent& e)
    {
        if (e.originalComponent != this)
        {
            if (mouseDragSource != nullptr)
                mouseDragSource->removeMouseListener (this);

            // (note: use a local copy of this in case the callback runs
            // a modal loop and deletes this object before the method completes)
            DragAndDropTarget::SourceDetails details (sourceDetails);
            DragAndDropTarget* finalTarget = nullptr;

            if (! isDoingExternalDrag)
            {
                const bool wasVisible = isVisible();
                setVisible (false);
                Component* unused;
                finalTarget = findTarget (e.getScreenPosition(), details.localPosition, unused);

                if (wasVisible) // fade the component and remove it - it'll be deleted later by the timer callback
                    dismissWithAnimation (finalTarget == nullptr);
            }

            if (getParentComponent() != nullptr)
                getParentComponent()->removeChildComponent (this);

            if (finalTarget != nullptr)
            {
                currentlyOverComp = nullptr;
                finalTarget->itemDropped (details);
            }

            // careful - this object could now be deleted..
        }
    }

    void mouseDrag (const MouseEvent& e)
    {
        if (e.originalComponent != this)
            updateLocation (true, e.getScreenPosition());
    }

    void updateLocation (const bool canDoExternalDrag, const Point<int>& screenPos)
    {
        DragAndDropTarget::SourceDetails details (sourceDetails);

        setNewScreenPos (screenPos);

        Component* newTargetComp;
        DragAndDropTarget* const newTarget = findTarget (screenPos, details.localPosition, newTargetComp);

        setVisible (newTarget == nullptr || newTarget->shouldDrawDragImageWhenOver());

        if (newTargetComp != currentlyOverComp)
        {
            DragAndDropTarget* const lastTarget = getCurrentlyOver();

            if (lastTarget != nullptr && details.sourceComponent != nullptr
                  && lastTarget->isInterestedInDragSource (details))
                lastTarget->itemDragExit (details);

            currentlyOverComp = newTargetComp;

            if (newTarget != nullptr
                  && newTarget->isInterestedInDragSource (details))
                newTarget->itemDragEnter (details);
        }

        sendDragMove (details);

        if (canDoExternalDrag && getCurrentlyOver() == nullptr)
            checkForExternalDrag (details, screenPos);
    }

    void timerCallback()
    {
        if (sourceDetails.sourceComponent == nullptr)
        {
            delete this;
        }
        else if (! isMouseButtonDownAnywhere())
        {
            if (mouseDragSource != nullptr)
                mouseDragSource->removeMouseListener (this);

            delete this;
        }
    }

private:
    DragAndDropTarget::SourceDetails sourceDetails;
    Image image;
    DragAndDropContainer& owner;
    WeakReference<Component> mouseDragSource, currentlyOverComp;
    const Point<int> imageOffset;
    bool hasCheckedForExternalDrag, isDoingExternalDrag;

    DragAndDropTarget* getCurrentlyOver() const noexcept
    {
        return dynamic_cast <DragAndDropTarget*> (currentlyOverComp.get());
    }

    DragAndDropTarget* findTarget (const Point<int>& screenPos, Point<int>& relativePos,
                                   Component*& resultComponent) const
    {
        Component* hit = getParentComponent();

        if (hit == nullptr)
            hit = Desktop::getInstance().findComponentAt (screenPos);
        else
            hit = hit->getComponentAt (hit->getLocalPoint (nullptr, screenPos));

        // (note: use a local copy of this in case the callback runs
        // a modal loop and deletes this object before the method completes)
        const DragAndDropTarget::SourceDetails details (sourceDetails);

        while (hit != nullptr)
        {
            DragAndDropTarget* const ddt = dynamic_cast <DragAndDropTarget*> (hit);

            if (ddt != nullptr && ddt->isInterestedInDragSource (details))
            {
                relativePos = hit->getLocalPoint (nullptr, screenPos);
                resultComponent = hit;
                return ddt;
            }

            hit = hit->getParentComponent();
        }

        resultComponent = nullptr;
        return nullptr;
    }

    void setNewScreenPos (const Point<int>& screenPos)
    {
        Point<int> newPos (screenPos - imageOffset);

        if (getParentComponent() != nullptr)
            newPos = getParentComponent()->getLocalPoint (nullptr, newPos);

        setTopLeftPosition (newPos);
    }

    void sendDragMove (DragAndDropTarget::SourceDetails& details) const
    {
        DragAndDropTarget* const target = getCurrentlyOver();

        if (target != nullptr && target->isInterestedInDragSource (details))
            target->itemDragMove (details);
    }

    void checkForExternalDrag (DragAndDropTarget::SourceDetails& details, const Point<int>& screenPos)
    {
        if (! hasCheckedForExternalDrag)
        {
            if (Desktop::getInstance().findComponentAt (screenPos) == nullptr)
            {
                hasCheckedForExternalDrag = true;
                StringArray files;
                bool canMoveFiles = false;

                if (owner.shouldDropFilesWhenDraggedExternally (details, files, canMoveFiles)
                     && files.size() > 0)
                {
                    WeakReference<Component> thisWeakRef (this);
                    setVisible (false);
                    isDoingExternalDrag = true;

                    if (ModifierKeys::getCurrentModifiersRealtime().isAnyMouseButtonDown())
                        DragAndDropContainer::performExternalDragDropOfFiles (files, canMoveFiles);

                    delete thisWeakRef.get();
                    return;
                }
            }
        }
    }

    void dismissWithAnimation (const bool shouldSnapBack)
    {
        setVisible (true);
        ComponentAnimator& animator = Desktop::getInstance().getAnimator();

        if (shouldSnapBack && sourceDetails.sourceComponent != nullptr)
        {
            const Point<int> target (sourceDetails.sourceComponent->localPointToGlobal (sourceDetails.sourceComponent->getLocalBounds().getCentre()));
            const Point<int> ourCentre (localPointToGlobal (getLocalBounds().getCentre()));

            animator.animateComponent (this,
                                       getBounds() + (target - ourCentre),
                                       0.0f, 120,
                                       true, 1.0, 1.0);
        }
        else
        {
            animator.fadeOut (this, 120);
        }
    }

    JUCE_DECLARE_NON_COPYABLE (DragImageComponent);
};


//==============================================================================
DragAndDropContainer::DragAndDropContainer()
{
}

DragAndDropContainer::~DragAndDropContainer()
{
    dragImageComponent = nullptr;
}

void DragAndDropContainer::startDragging (const var& sourceDescription,
                                          Component* sourceComponent,
                                          const Image& dragImage_,
                                          const bool allowDraggingToExternalWindows,
                                          const Point<int>* imageOffsetFromMouse)
{
    Image dragImage (dragImage_);

    if (dragImageComponent == nullptr)
    {
        MouseInputSource* draggingSource = Desktop::getInstance().getDraggingMouseSource (0);

        if (draggingSource == nullptr || ! draggingSource->isDragging())
        {
            jassertfalse;   // You must call startDragging() from within a mouseDown or mouseDrag callback!
            return;
        }

        const Point<int> lastMouseDown (Desktop::getLastMouseDownPosition());
        Point<int> imageOffset;

        if (dragImage.isNull())
        {
            dragImage = sourceComponent->createComponentSnapshot (sourceComponent->getLocalBounds())
                            .convertedToFormat (Image::ARGB);

            dragImage.multiplyAllAlphas (0.6f);

            const int lo = 150;
            const int hi = 400;

            Point<int> relPos (sourceComponent->getLocalPoint (nullptr, lastMouseDown));
            Point<int> clipped (dragImage.getBounds().getConstrainedPoint (relPos));
            Random random;

            for (int y = dragImage.getHeight(); --y >= 0;)
            {
                const double dy = (y - clipped.getY()) * (y - clipped.getY());

                for (int x = dragImage.getWidth(); --x >= 0;)
                {
                    const int dx = x - clipped.getX();
                    const int distance = roundToInt (std::sqrt (dx * dx + dy));

                    if (distance > lo)
                    {
                        const float alpha = (distance > hi) ? 0
                                                            : (hi - distance) / (float) (hi - lo)
                                                               + random.nextFloat() * 0.008f;

                        dragImage.multiplyAlphaAt (x, y, alpha);
                    }
                }
            }

            imageOffset = clipped;
        }
        else
        {
            if (imageOffsetFromMouse == nullptr)
                imageOffset = dragImage.getBounds().getCentre();
            else
                imageOffset = dragImage.getBounds().getConstrainedPoint (-*imageOffsetFromMouse);
        }

        dragImageComponent = new DragImageComponent (dragImage, sourceDescription, sourceComponent,
                                                     draggingSource->getComponentUnderMouse(), *this, imageOffset);

        currentDragDesc = sourceDescription;

        if (allowDraggingToExternalWindows)
        {
            if (! Desktop::canUseSemiTransparentWindows())
                dragImageComponent->setOpaque (true);

            dragImageComponent->addToDesktop (ComponentPeer::windowIgnoresMouseClicks
                                               | ComponentPeer::windowIsTemporary
                                               | ComponentPeer::windowIgnoresKeyPresses);
        }
        else
        {
            Component* const thisComp = dynamic_cast <Component*> (this);

            if (thisComp == nullptr)
            {
                jassertfalse;   // Your DragAndDropContainer needs to be a Component!
                return;
            }

            thisComp->addChildComponent (dragImageComponent);
        }

        static_cast <DragImageComponent*> (dragImageComponent.get())->updateLocation (false, lastMouseDown);
        dragImageComponent->setVisible (true);

       #if JUCE_WINDOWS
        // Under heavy load, the layered window's paint callback can often be lost by the OS,
        // so forcing a repaint at least once makes sure that the window becomes visible..
        ComponentPeer* const peer = dragImageComponent->getPeer();
        if (peer != nullptr)
            peer->performAnyPendingRepaintsNow();
       #endif
    }
}

bool DragAndDropContainer::isDragAndDropActive() const
{
    return dragImageComponent != nullptr;
}

String DragAndDropContainer::getCurrentDragDescription() const
{
    return dragImageComponent != nullptr ? currentDragDesc
                                         : String::empty;
}

DragAndDropContainer* DragAndDropContainer::findParentDragContainerFor (Component* c)
{
    return c != nullptr ? c->findParentComponentOfClass<DragAndDropContainer>() : nullptr;
}

bool DragAndDropContainer::shouldDropFilesWhenDraggedExternally (const DragAndDropTarget::SourceDetails&, StringArray&, bool&)
{
    return false;
}

//==============================================================================
DragAndDropTarget::SourceDetails::SourceDetails (const var& description_, Component* sourceComponent_, const Point<int>& localPosition_) noexcept
    : description (description_),
      sourceComponent (sourceComponent_),
      localPosition (localPosition_)
{
}

void DragAndDropTarget::itemDragEnter (const SourceDetails&)  {}
void DragAndDropTarget::itemDragMove  (const SourceDetails&)  {}
void DragAndDropTarget::itemDragExit  (const SourceDetails&)  {}
bool DragAndDropTarget::shouldDrawDragImageWhenOver()         { return true; }

//==============================================================================
void FileDragAndDropTarget::fileDragEnter (const StringArray&, int, int)  {}
void FileDragAndDropTarget::fileDragMove  (const StringArray&, int, int)  {}
void FileDragAndDropTarget::fileDragExit  (const StringArray&)            {}

void TextDragAndDropTarget::textDragEnter (const String&, int, int)  {}
void TextDragAndDropTarget::textDragMove  (const String&, int, int)  {}
void TextDragAndDropTarget::textDragExit  (const String&)            {}