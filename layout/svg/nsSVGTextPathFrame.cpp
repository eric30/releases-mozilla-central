/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Main header first:
#include "nsSVGTextPathFrame.h"

// Keep others in (case-insensitive) order:
#include "gfx2DGlue.h"
#include "gfxPath.h"
#include "nsContentUtils.h"
#include "nsSVGEffects.h"
#include "nsSVGLength2.h"
#include "mozilla/dom/SVGPathElement.h"
#include "mozilla/dom/SVGTextPathElement.h"
#include "mozilla/gfx/2D.h"
#include "SVGLengthList.h"

using namespace mozilla;
using namespace mozilla::dom;
using namespace mozilla::gfx;

//----------------------------------------------------------------------
// Implementation

nsIFrame*
NS_NewSVGTextPathFrame(nsIPresShell* aPresShell, nsStyleContext* aContext)
{
  return new (aPresShell) nsSVGTextPathFrame(aContext);
}

NS_IMPL_FRAMEARENA_HELPERS(nsSVGTextPathFrame)

#ifdef DEBUG
void
nsSVGTextPathFrame::Init(nsIContent* aContent,
                         nsIFrame* aParent,
                         nsIFrame* aPrevInFlow)
{
  NS_ASSERTION(aParent, "null parent");

  nsIFrame* ancestorFrame = nsSVGUtils::GetFirstNonAAncestorFrame(aParent);
  NS_ASSERTION(ancestorFrame, "Must have ancestor");

  NS_ASSERTION(ancestorFrame->GetType() == nsGkAtoms::svgTextFrame,
               "trying to construct an SVGTextPathFrame for an invalid "
               "container");

  NS_ASSERTION(aContent->IsSVG(nsGkAtoms::textPath),
               "Content is not an SVG textPath");

  nsSVGTextPathFrameBase::Init(aContent, aParent, aPrevInFlow);
}
#endif /* DEBUG */

nsIAtom *
nsSVGTextPathFrame::GetType() const
{
  return nsGkAtoms::svgTextPathFrame;
}

void
nsSVGTextPathFrame::GetXY(SVGUserUnitList *aX, SVGUserUnitList *aY)
{
  // 'x' and 'y' don't apply to 'textPath'
  aX->Clear();
  aY->Clear();
}

void
nsSVGTextPathFrame::GetDxDy(SVGUserUnitList *aDx, SVGUserUnitList *aDy)
{
  // 'dx' and 'dy' don't apply to 'textPath'
  aDx->Clear();
  aDy->Clear();
}

const SVGNumberList*
nsSVGTextPathFrame::GetRotate()
{
  return nullptr;
}

//----------------------------------------------------------------------
// nsSVGTextPathFrame methods:

nsIFrame *
nsSVGTextPathFrame::GetPathFrame()
{
  nsSVGTextPathProperty *property = static_cast<nsSVGTextPathProperty*>
    (Properties().Get(nsSVGEffects::HrefProperty()));

  if (!property) {
    SVGTextPathElement *tp = static_cast<SVGTextPathElement*>(mContent);
    nsAutoString href;
    tp->mStringAttributes[SVGTextPathElement::HREF].GetAnimValue(href, tp);
    if (href.IsEmpty()) {
      return nullptr; // no URL
    }

    nsCOMPtr<nsIURI> targetURI;
    nsCOMPtr<nsIURI> base = mContent->GetBaseURI();
    nsContentUtils::NewURIWithDocumentCharset(getter_AddRefs(targetURI), href,
                                              mContent->GetCurrentDoc(), base);

    property =
      nsSVGEffects::GetTextPathProperty(targetURI, this, nsSVGEffects::HrefProperty());
    if (!property)
      return nullptr;
  }

  nsIFrame *frame = property->GetReferencedFrame(nsGkAtoms::svgPathGeometryFrame, nullptr);
  return frame && frame->GetContent()->Tag() == nsGkAtoms::path ? frame : nullptr;
}

TemporaryRef<Path>
nsSVGTextPathFrame::GetPath()
{
  nsIFrame *pathFrame = GetPathFrame();

  if (pathFrame) {
    nsSVGPathGeometryElement *element =
      static_cast<nsSVGPathGeometryElement*>(pathFrame->GetContent());

    RefPtr<Path> path = element->GetPathForLengthOrPositionMeasuring();

    gfxMatrix matrix = element->PrependLocalTransformsTo(gfxMatrix());
    if (!matrix.IsIdentity()) {
      RefPtr<PathBuilder> builder =
        path->TransformedCopyToBuilder(ToMatrix(matrix));
      path = builder->Finish();
    }

    return path.forget();
  }
  return nullptr;
}
 
gfxFloat
nsSVGTextPathFrame::GetStartOffset()
{
  SVGTextPathElement *tp = static_cast<SVGTextPathElement*>(mContent);
  nsSVGLength2 *length = &tp->mLengthAttributes[SVGTextPathElement::STARTOFFSET];

  if (length->IsPercentage()) {
    RefPtr<Path> path = GetPath();
    return path ? (length->GetAnimValInSpecifiedUnits() * path->ComputeLength() / 100.0) : 0.0;
  }
  return length->GetAnimValue(tp) * GetOffsetScale();
}

gfxFloat
nsSVGTextPathFrame::GetOffsetScale()
{
  nsIFrame *pathFrame = GetPathFrame();
  if (!pathFrame)
    return 1.0;

  return static_cast<SVGPathElement*>(pathFrame->GetContent())->
    GetPathLengthScale(SVGPathElement::eForTextPath);
}

//----------------------------------------------------------------------
// nsIFrame methods

NS_IMETHODIMP
nsSVGTextPathFrame::AttributeChanged(int32_t         aNameSpaceID,
                                     nsIAtom*        aAttribute,
                                     int32_t         aModType)
{
  if (aNameSpaceID == kNameSpaceID_None &&
      aAttribute == nsGkAtoms::startOffset) {
    nsSVGEffects::InvalidateRenderingObservers(this);
    nsSVGUtils::ScheduleReflowSVG(this);
    NotifyGlyphMetricsChange();
  } else if (aNameSpaceID == kNameSpaceID_XLink &&
             aAttribute == nsGkAtoms::href) {
    nsSVGEffects::InvalidateRenderingObservers(this);
    nsSVGUtils::ScheduleReflowSVG(this);
    // Blow away our reference, if any
    Properties().Delete(nsSVGEffects::HrefProperty());
    NotifyGlyphMetricsChange();
  }

  return NS_OK;
}
