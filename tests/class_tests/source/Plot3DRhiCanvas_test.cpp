// Copyright (c) 2002-present, OpenMS Inc. -- EKU Tuebingen, ETH Zurich, and FU Berlin
// SPDX-License-Identifier: BSD-3-Clause
//
// --------------------------------------------------------------------------
// $Maintainer: Timo Sachsenberg $
// $Authors: Cornelia Friedle $
// --------------------------------------------------------------------------

#include <OpenMS/CONCEPT/ClassTest.h>

///////////////////////////

#include <OpenMS/VISUAL/Plot3DCanvas.h>
#include <OpenMS/VISUAL/Plot3DRhiCanvas.h>
#include <OpenMS/VISUAL/Plot3DWidget.h>
#include <OpenMS/DATASTRUCTURES/Param.h>
#include <OpenMS/KERNEL/MSExperiment.h>
#include <OpenMS/METADATA/AnnotatedMSRun.h>
#include <OpenMS/KERNEL/OnDiscMSExperiment.h>

#include <QApplication>
#include <QFile>
#include <QImage>
#include <QSet>

#include <cstdlib>
#include <memory>
///////////////////////////

using namespace OpenMS;
using namespace std;

namespace
{
  /// a small peak map: four spectra, five peaks each, rising intensities
  LayerDataBase::ExperimentSharedPtrType makeExperiment()
  {
    MSExperiment experiment;
    for (int s = 0; s < 4; ++s)
    {
      MSSpectrum spectrum;
      spectrum.setRT(10.0 + s);
      spectrum.setMSLevel(1);
      for (int p = 0; p < 5; ++p)
      {
        Peak1D peak;
        peak.setMZ(100.0 + 100.0 * p);
        peak.setIntensity(float(1000.0 * (p + 1) * (s + 1)));
        spectrum.push_back(peak);
      }
      experiment.addSpectrum(spectrum);
    }
    experiment.updateRanges();
    return std::make_shared<AnnotatedMSRun>(std::move(experiment));
  }

  /// number of distinct colours in an image; a drawn peak map has more than one
  int distinctColors(const QImage& image)
  {
    QSet<QRgb> colors;
    for (int y = 0; y < image.height(); ++y)
    {
      for (int x = 0; x < image.width(); ++x)
      {
        colors.insert(image.pixel(x, y));
      }
    }
    return colors.size();
  }
}

START_TEST(Plot3DRhiCanvas, "$Id$")

/////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////

QApplication app(argc, argv);

START_SECTION((shader resources are compiled into the GUI library))
{
  TEST_EQUAL(QFile::exists(":/OpenMS/shaders/plot3d.vert.qsb"), true)
  TEST_EQUAL(QFile::exists(":/OpenMS/shaders/plot3d.frag.qsb"), true)
}
END_SECTION

START_SECTION((geometry is built from the layer data without any GPU))
{
  // Four spectra with five peaks each become one stick (two vertices) per peak
  // in the default translate mode; the ground is two triangles, the axes three
  // lines, and every visible axis carries ticks. None of this needs a frame.
  Param preferences;
  Plot3DWidget widget(preferences);
  Plot3DRhiCanvas* canvas = widget.canvas()->rhiwidget();
  TEST_NOT_EQUAL(canvas, nullptr)
  TEST_EQUAL(widget.canvas()->addPeakLayer(makeExperiment(), std::make_shared<OnDiscMSExperiment>(), "", "test", false), true)

  canvas->rebuildGeometry();
  const Plot3DRhiCanvas::GeometryVertexCounts counts = canvas->geometryVertexCounts();
  TEST_EQUAL(counts.stickdata, 2 * 4 * 5)
  TEST_EQUAL(counts.ground, 6)
  TEST_EQUAL(counts.axes, 6)
  TEST_EQUAL(counts.axes_ticks > 0, true)
  TEST_EQUAL(counts.axes_ticks % 2, 0)
}
END_SECTION

START_SECTION((rendering either produces a frame or fails gracefully on a headless platform))
{
  // QRhiWidget needs a platform with RHI-based widget compositing; the minimal
  // and offscreen platforms used on headless runners have none. Both outcomes
  // are valid here; a crash or a half state is not.
  Param preferences;
  Plot3DWidget widget(preferences);
  Plot3DRhiCanvas* canvas = widget.canvas()->rhiwidget();
  bool render_failed = false;
  QObject::connect(canvas, &QRhiWidget::renderFailed, [&render_failed]() { render_failed = true; });
  widget.canvas()->addPeakLayer(makeExperiment(), std::make_shared<OnDiscMSExperiment>(), "", "test", false);
  widget.resize(400, 300);

  const QImage frame = canvas->grabFramebuffer();
  if (render_failed)
  {
    STATUS("no RHI-capable platform: rendering declined cleanly")
    TEST_EQUAL(frame.isNull(), true)
  }
  else
  {
    STATUS("rendered through the platform backend")
    // grabFramebuffer() reads back a freshly rendered frame; frameSubmitted() is
    // only emitted by the normal paint path, so the image itself is the evidence.
    TEST_EQUAL(frame.isNull(), false)
    TEST_EQUAL(distinctColors(frame) > 1, true)
  }
}
END_SECTION

START_SECTION(([EXTRA] the platform backend draws the peak map when a GPU is available))
{
  // Opt-in only: renders through the real backend (Metal, Direct3D, Vulkan or
  // OpenGL) and saves the frame for inspection. Never run on a headless runner.
  const char* output = std::getenv("OPENMS_PLOT3D_RENDER_TEST_IMAGE");
  if (output == nullptr)
  {
    NOT_TESTABLE
  }
  else
  {
    Param preferences;
    Plot3DWidget widget(preferences);
    Plot3DRhiCanvas* canvas = widget.canvas()->rhiwidget();
    bool render_failed = false;
    QObject::connect(canvas, &QRhiWidget::renderFailed, [&render_failed]() { render_failed = true; });
    widget.canvas()->addPeakLayer(makeExperiment(), std::make_shared<OnDiscMSExperiment>(), "", "test", false);
    widget.resize(640, 480);
    widget.show();
    QApplication::processEvents();
    const QImage frame = canvas->grabFramebuffer();
    TEST_EQUAL(render_failed, false)
    TEST_EQUAL(frame.isNull(), false)
    TEST_EQUAL(distinctColors(frame) > 1, true)
    TEST_EQUAL(frame.save(QString::fromUtf8(output)), true)
  }
}
END_SECTION

/////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////
END_TEST
