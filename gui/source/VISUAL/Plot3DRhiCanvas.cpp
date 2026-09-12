// Copyright (c) 2002-present, OpenMS Inc. -- EKU Tuebingen, ETH Zurich, and FU Berlin
// SPDX-License-Identifier: BSD-3-Clause
//
// --------------------------------------------------------------------------
// $Maintainer: Timo Sachsenberg $
// $Authors: Marc Sturm $
// --------------------------------------------------------------------------

#include <OpenMS/VISUAL/Plot3DRhiCanvas.h>

#include <OpenMS/VISUAL/Plot3DCanvas.h>
#include <OpenMS/VISUAL/AxisTickCalculator.h>
#include <OpenMS/VISUAL/LayerDataPeak.h>

#include <OpenMS/MATH/MathFunctions.h>
#include <OpenMS/VISUAL/MISC/Qt5Port.h>

#include <rhi/qrhi.h>

#include <QFile>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>

#include <map>

using std::max;

namespace OpenMS
{
  namespace Internal
  {
    /// Transparent child widget that paints the axis legend on top of the rendered texture
    class Plot3DLegendOverlay :
      public QWidget
    {
public:
      explicit Plot3DLegendOverlay(Plot3DRhiCanvas& canvas) :
        QWidget(&canvas),
        canvas_(canvas)
      {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);
      }

protected:
      void paintEvent(QPaintEvent * /* e */) override
      {
        QPainter painter(this);
        canvas_.drawAxesLegend_(painter);
      }

private:
      Plot3DRhiCanvas& canvas_;
    };
  }

  namespace
  {
    /// interleaved xyz position and rgba colour
    constexpr int VERTEX_FLOATS = 7;
    /// std140 layout: mat4 (64 bytes) followed by one float, padded to 16
    constexpr int UNIFORM_BYTES = 80;

    QShader loadShader(const QString& name)
    {
      QFile file(name);
      if (!file.open(QIODevice::ReadOnly))
      {
        return QShader();
      }
      return QShader::fromSerialized(file.readAll());
    }
  }

  /// Everything that lives on the GPU for one QRhi instance
  struct Plot3DRhiCanvas::RhiState
  {
    /// vertex buffer of one uploaded batch
    struct Gpu
    {
      std::unique_ptr<QRhiBuffer> vbuf;
      int vertex_count = 0;
      Topology topology = Topology::LINES;
      float line_width = 1.0f;
    };

    QRhi* rhi = nullptr;
    int sample_count = 1;
    bool wide_lines = false;
    QShader vertex_shader;
    QShader fragment_shader;
    std::unique_ptr<QRhiBuffer> ubuf;
    std::unique_ptr<QRhiShaderResourceBindings> srb;
    std::unique_ptr<QRhiGraphicsPipeline> triangles;
    std::unique_ptr<QRhiGraphicsPipeline> points;
    /// one pipeline per line width, because width is pipeline state
    std::map<float, std::unique_ptr<QRhiGraphicsPipeline>> lines;
    Gpu ground, axes, ticks, grid, sticks;

    std::unique_ptr<QRhiGraphicsPipeline> makePipeline(QRhiRenderPassDescriptor* rp,
                                                       QRhiGraphicsPipeline::Topology topology,
                                                       float line_width)
    {
      std::unique_ptr<QRhiGraphicsPipeline> ps(rhi->newGraphicsPipeline());
      ps->setTopology(topology);
      ps->setShaderStages({{QRhiShaderStage::Vertex, vertex_shader},
                           {QRhiShaderStage::Fragment, fragment_shader}});
      QRhiVertexInputLayout layout;
      layout.setBindings({{VERTEX_FLOATS * sizeof(float)}});
      layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float3, 0},
                            {0, 1, QRhiVertexInputAttribute::Float4, 3 * sizeof(float)}});
      ps->setVertexInputLayout(layout);
      ps->setShaderResourceBindings(srb.get());
      ps->setRenderPassDescriptor(rp);
      ps->setSampleCount(sample_count);
      ps->setDepthTest(true);
      ps->setDepthWrite(true);
      if (topology == QRhiGraphicsPipeline::Lines && wide_lines)
      {
        ps->setLineWidth(line_width);
      }
      QRhiGraphicsPipeline::TargetBlend blend;
      blend.enable = true;
      blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
      blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
      blend.srcAlpha = QRhiGraphicsPipeline::One;
      blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
      ps->setTargetBlends({blend});
      ps->create();
      return ps;
    }

    QRhiGraphicsPipeline* linesPipeline(QRhiRenderPassDescriptor* rp, float line_width)
    {
      const float key = wide_lines ? line_width : 1.0f;
      auto& ps = lines[key];
      if (!ps)
      {
        ps = makePipeline(rp, QRhiGraphicsPipeline::Lines, key);
      }
      return ps.get();
    }

    void upload(Gpu& gpu, const VertexBatch& batch, QRhiResourceUpdateBatch* updates)
    {
      gpu.vertex_count = batch.vertexCount();
      gpu.topology = batch.topology;
      gpu.line_width = batch.line_width;
      gpu.vbuf.reset();
      if (gpu.vertex_count == 0)
      {
        return;
      }
      const quint32 bytes = quint32(batch.data.size() * sizeof(float));
      gpu.vbuf.reset(rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, bytes));
      gpu.vbuf->create();
      updates->uploadStaticBuffer(gpu.vbuf.get(), batch.data.data());
    }

    void draw(QRhiCommandBuffer* cb, QRhiRenderPassDescriptor* rp, const QSize& size, const Gpu& gpu)
    {
      if (!gpu.vbuf || gpu.vertex_count == 0)
      {
        return;
      }
      QRhiGraphicsPipeline* ps = nullptr;
      switch (gpu.topology)
      {
        case Topology::TRIANGLES: ps = triangles.get(); break;
        case Topology::POINTS: ps = points.get(); break;
        case Topology::LINES: ps = linesPipeline(rp, gpu.line_width); break;
      }
      cb->setGraphicsPipeline(ps);
      cb->setViewport({0, 0, float(size.width()), float(size.height())});
      cb->setShaderResources();
      const QRhiCommandBuffer::VertexInput input(gpu.vbuf.get(), 0);
      cb->setVertexInput(0, 1, &input);
      cb->draw(gpu.vertex_count);
    }
  };

  void Plot3DRhiCanvas::VertexBatch::add(double x, double y, double z, const QColor& color)
  {
    data.push_back(float(x));
    data.push_back(float(y));
    data.push_back(float(z));
    data.push_back(float(color.redF()));
    data.push_back(float(color.greenF()));
    data.push_back(float(color.blueF()));
    data.push_back(float(color.alphaF()));
  }

  Plot3DRhiCanvas::Plot3DRhiCanvas(QWidget * parent, Plot3DCanvas & canvas_3d) :
    QRhiWidget(parent),
    rhi_state_(std::make_unique<RhiState>()),
    canvas_3d_(canvas_3d)
  {
    canvas_3d.rubber_band_.setParent(this);
    legend_overlay_ = new Internal::Plot3DLegendOverlay(*this);
    legend_overlay_->setGeometry(rect());

    x_label_ = toQString(std::string(Peak2D::shortDimensionName(Peak2D::MZ)) + " [" + std::string(Peak2D::shortDimensionUnit(Peak2D::MZ)) + "]");
    y_label_ = toQString(std::string(Peak2D::shortDimensionName(Peak2D::RT)) + " [" + std::string(Peak2D::shortDimensionUnit(Peak2D::RT)) + "]");

    //Set focus policy and mouse tracking in order to get keyboard events
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    corner_ = 100.0;
    near_ = 0.0;
    far_ = 600.0;
    zoom_ = 1.5;
    xrot_ = 220;
    yrot_ = 220;
    zrot_ = 0;
    trans_x_ = 0.0;
    trans_y_ = 0.0;
    width_ = float(width());
    height_ = float(height());
  }

  Plot3DRhiCanvas::~Plot3DRhiCanvas() = default;

  void Plot3DRhiCanvas::markGeometryDirty()
  {
    geometry_dirty_ = true;
  }

  void Plot3DRhiCanvas::rebuildGeometry()
  {
    geometry_dirty_ = false;
    rebuildGeometry_();
  }

  Plot3DRhiCanvas::GeometryVertexCounts Plot3DRhiCanvas::geometryVertexCounts() const
  {
    GeometryVertexCounts counts;
    counts.ground = ground_.vertexCount();
    counts.axes = axes_.vertexCount();
    counts.axes_ticks = axes_ticks_.vertexCount();
    counts.gridlines = gridlines_.vertexCount();
    counts.stickdata = stickdata_.vertexCount();
    return counts;
  }

  void Plot3DRhiCanvas::calculateGridLines_()
  {
    switch (canvas_3d_.intensity_mode_)
    {
    case PlotCanvas::IM_SNAP:
      updateIntensityScale();
      AxisTickCalculator::calcGridLines(0.0, int_scale_.max_[0], grid_intensity_);
      break;

    case PlotCanvas::IM_NONE:
      AxisTickCalculator::calcGridLines(0.0, canvas_3d_.overall_data_range_.getMaxIntensity(), grid_intensity_);
      break;

    case PlotCanvas::IM_PERCENTAGE:
      AxisTickCalculator::calcGridLines(0.0, 100.0, grid_intensity_);
      break;

    case PlotCanvas::IM_LOG:
      AxisTickCalculator::calcLogGridLines(0.0, log10(1 + max(0.0, canvas_3d_.overall_data_range_.getMaxIntensity())), grid_intensity_);
      break;
    }

    AxisTickCalculator::calcGridLines(canvas_3d_.visible_area_.getAreaUnit().getMinRT(), canvas_3d_.visible_area_.getAreaUnit().getMaxRT(), grid_rt_);
    AxisTickCalculator::calcGridLines(canvas_3d_.visible_area_.getAreaUnit().getMinMZ(), canvas_3d_.visible_area_.getAreaUnit().getMaxMZ(), grid_mz_);
  }

  void Plot3DRhiCanvas::updateMatrices_()
  {
    // Same view as the former glOrtho/glTranslate/glRotate sequence, kept in
    // OpenGL clip-space conventions; render() adds the backend correction.
    projection_.setToIdentity();
    projection_.ortho(float(-corner_ * zoom_), float(corner_ * zoom_),
                      float(-corner_ * zoom_), float(corner_ * zoom_),
                      float(near_), float(far_));
    modelview_.setToIdentity();
    modelview_.translate(0.0f, 0.0f, float(-3.0 * corner_));
    modelview_.rotate(xrot_ / 16.0f, 1.0f, 0.0f, 0.0f);
    modelview_.rotate(yrot_ / 16.0f, 0.0f, 1.0f, 0.0f);
    modelview_.rotate(zrot_ / 16.0f, 0.0f, 0.0f, 1.0f);
    modelview_.translate(float(trans_x_), float(trans_y_), float(3.0 * corner_));
  }

  bool Plot3DRhiCanvas::project_(double objx, double objy, double objz, double * winx, double * winy) const
  {
    const QVector4D clip = projection_ * modelview_ * QVector4D(float(objx), float(objy), float(objz), 1.0f);
    if (clip.w() == 0.0f) { return false; }

    // transform homogeneous coordinates into normalized device coordinates
    const double ndc_x = clip.x() / clip.w();
    const double ndc_y = clip.y() / clip.w();

    // viewport transformation (0,0 is in corner of screen not in middle of screen)
    *winx = (1 + ndc_x) * width() / 2;
    *winy = (1 + ndc_y) * height() / 2;
    return true;
  }

  void Plot3DRhiCanvas::renderText_(QPainter& painter, double x, double y, double z, const QString & text) const
  {
    double text_x = 0, text_y = 0;
    if (!project_(x, y, z, &text_x, &text_y)) { return; }
    text_y = height() - text_y; // y is inverted
    painter.drawText(QPointF(text_x, text_y), text);
  }

  void Plot3DRhiCanvas::resizeEvent(QResizeEvent * e)
  {
    QRhiWidget::resizeEvent(e);
    width_ = float(e->size().width());
    height_ = float(e->size().height());
    legend_overlay_->setGeometry(rect());
  }

  void Plot3DRhiCanvas::rebuildGeometry_()
  {
    calculateGridLines_();

    //abort if no layers are displayed
    if (canvas_3d_.getLayerCount() == 0) { return; }

    if (canvas_3d_.action_mode_ == PlotCanvas::AM_ZOOM)
    {
      if (!canvas_3d_.rubber_band_.isVisible())
      {
        axes_ = makeAxes_();
        if (canvas_3d_.show_grid_)
        {
          gridlines_ = makeGridLines_();
        }
        xrot_ = 90 * 16;
        yrot_ = 0;
        zrot_ = 0;
        zoom_ = 1.25;
        stickdata_ = makeDataAsTopView_();
        axes_ticks_ = makeAxesTicks_();
      }
    }
    else if (canvas_3d_.action_mode_ == PlotCanvas::AM_TRANSLATE)
    {
      if (canvas_3d_.show_grid_) { gridlines_ = makeGridLines_(); }
      axes_ = makeAxes_();
      ground_ = makeGround_();
      x_1_ = 0.0;
      y_1_ = 0.0;
      x_2_ = 0.0;
      y_2_ = 0.0;
      stickdata_ = makeDataAsStick_();
      axes_ticks_ = makeAxesTicks_();
    }
    upload_dirty_ = true;
  }

  void Plot3DRhiCanvas::initialize(QRhiCommandBuffer * /* cb */)
  {
    RhiState& state = *rhi_state_;
    if (state.rhi == rhi() && state.sample_count == renderTarget()->sampleCount())
    {
      return;
    }
    // A new QRhi means every resource created against the old one is gone.
    state.lines.clear();
    state.triangles.reset();
    state.points.reset();
    state.srb.reset();
    state.ubuf.reset();
    for (RhiState::Gpu* gpu : {&state.ground, &state.axes, &state.ticks, &state.grid, &state.sticks})
    {
      gpu->vbuf.reset();
    }
    state.rhi = rhi();
    state.sample_count = renderTarget()->sampleCount();
    state.wide_lines = state.rhi->isFeatureSupported(QRhi::WideLines);
    state.vertex_shader = loadShader(QStringLiteral(":/OpenMS/shaders/plot3d.vert.qsb"));
    state.fragment_shader = loadShader(QStringLiteral(":/OpenMS/shaders/plot3d.frag.qsb"));

    state.ubuf.reset(state.rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, UNIFORM_BYTES));
    state.ubuf->create();
    state.srb.reset(state.rhi->newShaderResourceBindings());
    state.srb->setBindings({QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage, state.ubuf.get())});
    state.srb->create();

    QRhiRenderPassDescriptor* rp = renderTarget()->renderPassDescriptor();
    state.triangles = state.makePipeline(rp, QRhiGraphicsPipeline::Triangles, 1.0f);
    state.points = state.makePipeline(rp, QRhiGraphicsPipeline::Points, 1.0f);
    upload_dirty_ = true;
  }

  void Plot3DRhiCanvas::render(QRhiCommandBuffer * cb)
  {
    RhiState& state = *rhi_state_;
    if (geometry_dirty_)
    {
      geometry_dirty_ = false;
      rebuildGeometry_();
    }
    updateMatrices_();

    QRhiResourceUpdateBatch* updates = state.rhi->nextResourceUpdateBatch();
    if (upload_dirty_)
    {
      upload_dirty_ = false;
      state.upload(state.ground, ground_, updates);
      state.upload(state.axes, axes_, updates);
      state.upload(state.ticks, axes_ticks_, updates);
      state.upload(state.grid, gridlines_, updates);
      state.upload(state.sticks, stickdata_, updates);
    }
    const QMatrix4x4 mvp = state.rhi->clipSpaceCorrMatrix() * projection_ * modelview_;
    updates->updateDynamicBuffer(state.ubuf.get(), 0, 64, mvp.constData());
    const float point_size = 3.0f;
    updates->updateDynamicBuffer(state.ubuf.get(), 64, sizeof(float), &point_size);

    const QColor background(toQString(std::string(canvas_3d_.param_.getValue("background_color").toString())));
    const QSize size = renderTarget()->pixelSize();
    QRhiRenderPassDescriptor* rp = renderTarget()->renderPassDescriptor();
    cb->beginPass(renderTarget(), background, {1.0f, 0}, updates);

    if (canvas_3d_.getLayerCount() != 0)
    {
      state.draw(cb, rp, size, state.ground);
      if (canvas_3d_.show_grid_) { state.draw(cb, rp, size, state.grid); }
      state.draw(cb, rp, size, state.axes);
      state.draw(cb, rp, size, state.ticks);
      if (canvas_3d_.action_mode_ == PlotCanvas::AM_ZOOM
       || canvas_3d_.action_mode_ == PlotCanvas::AM_TRANSLATE)
      {
        state.draw(cb, rp, size, state.sticks);
      }
    }

    cb->endPass();
    legend_overlay_->update();
  }

  void Plot3DRhiCanvas::resetTranslation()
  {
    trans_x_ = 0.0;
    trans_y_ = 0.0;
  }

  void Plot3DRhiCanvas::storeRotationAndZoom()
  {
    xrot_tmp_ = xrot_;
    yrot_tmp_ = yrot_;
    zrot_tmp_ = zrot_;
    zoom_tmp_ = zoom_;
  }

  void Plot3DRhiCanvas::restoreRotationAndZoom()
  {
    xrot_ = xrot_tmp_;
    yrot_ = yrot_tmp_;
    zrot_ = zrot_tmp_;
    zoom_ = zoom_tmp_;
  }

  void Plot3DRhiCanvas::drawAxesLegend_(QPainter& painter)
  {
    if (canvas_3d_.getLayerCount() == 0) { return; }

    QFont font("Typewriter");
    font.setPixelSize(10);
    painter.setFont(font);
    painter.setPen(Qt::black);

    QString text;

    // Draw x and y axis legend
    if (canvas_3d_.legend_shown_)
    {
      font.setPixelSize(12);
      painter.setFont(font);
      renderText_(painter, 0.0, -corner_ - 20.0, -near_ - 2 * corner_ + 20.0, x_label_);
      renderText_(painter, -corner_ - 20.0, -corner_ - 20.0, -near_ - 3 * corner_, y_label_);
      font.setPixelSize(10);
      painter.setFont(font);
    }

    // RT tick labels
    {
      if (grid_rt_.size() > 0)
      {
        for (Size i = 0; i < grid_rt_[0].size(); i++)
        {
          text = QString::number(grid_rt_[0][i]);
          renderText_(painter, -corner_ - 15.0, -corner_ - 5.0, -near_ - 2 * corner_ - scaledRT_(grid_rt_[0][i]), text);
        }
      }
      if (zoom_ < 3.0 && grid_rt_.size() >= 2)
      {
        for (Size i = 0; i < grid_rt_[1].size(); i++)
        {
          text = QString::number(grid_rt_[1][i]);
          renderText_(painter, -corner_ - 15.0, -corner_ - 5.0, -near_ - 2 * corner_ - scaledRT_(grid_rt_[1][i]), text);
        }
      }
      if (zoom_ < 2.0 && grid_rt_.size() >= 3)
      {
        for (Size i = 0; i < grid_rt_[2].size(); i++)
        {
          text = QString::number(grid_rt_[2][i]);
          renderText_(painter, -corner_ - 15.0, -corner_ - 5.0, -near_ - 2 * corner_ - scaledRT_(grid_rt_[2][i]), text);
        }
      }
    }

    // m/z tick labels
    {
      if (grid_mz_.size() > 0)
      {
        for (Size i = 0; i < grid_mz_[0].size(); i++)
        {
          text = QString::number(grid_mz_[0][i]);
          renderText_(painter, -corner_ - text.length() + scaledMZ_(grid_mz_[0][i]), -corner_ - 5.0, -near_ - 2 * corner_ + 15.0, text);
        }
      }
      if (zoom_ < 3.0 && grid_mz_.size() >= 2)
      {
        for (Size i = 0; i < grid_mz_[1].size(); i++)
        {
          text = QString::number(grid_mz_[1][i]);
          renderText_(painter, -corner_ - text.length() + scaledMZ_(grid_mz_[1][i]), -corner_ - 5.0, -near_ - 2 * corner_ + 15.0, text);
        }
      }
      if (zoom_ < 2.0 && grid_mz_.size() >= 3)
      {
        for (Size i = 0; i < grid_mz_[2].size(); i++)
        {
          text = QString::number(grid_mz_[2][i]);
          renderText_(painter, -corner_ - text.length() + scaledMZ_(grid_mz_[2][i]), -corner_ - 5.0, -near_ - 2 * corner_ + 15.0, text);
        }
      }
    }

    // draw intensity legend if not in zoom mode
    if (canvas_3d_.action_mode_ != PlotCanvas::AM_ZOOM)
    {
      switch (canvas_3d_.intensity_mode_)
      {
      case PlotCanvas::IM_LOG:

        if (canvas_3d_.legend_shown_)
        {
          font.setPixelSize(12);
          painter.setFont(font);
          text = QString("intensity log");
          renderText_(painter, -corner_ - 20.0, corner_ + 10.0, -near_ - 2 * corner_ + 20.0, text);
          font.setPixelSize(10);
          painter.setFont(font);
        }

        if (zoom_ < 3.0 && grid_intensity_.size() >= 2)
        {
          for (Size i = 0; i < grid_intensity_[0].size(); i++)
          {
            double intensity = (double)grid_intensity_[0][i];
            text = QString("%1").arg(intensity, 0, 'f', 0);
            renderText_(painter, -corner_ - text.length() - width_ / 200.0 - 5.0, -corner_ + scaledIntensity_(pow(10.0, grid_intensity_[0][i]) - 1, canvas_3d_.layers_.getCurrentLayerIndex()), -near_ - 2 * corner_, text);
          }
        }
        break;

      case PlotCanvas::IM_PERCENTAGE:
        if (canvas_3d_.legend_shown_)
        {
          font.setPixelSize(12);
          painter.setFont(font);
          renderText_(painter, -corner_ - 20.0, corner_ + 10.0, -near_ - 2 * corner_ + 20.0, "intensity %");
          font.setPixelSize(10);
          painter.setFont(font);
        }

        for (Size i = 0; i < grid_intensity_[0].size(); i++)
        {
          text = QString::number(grid_intensity_[0][i]);
          renderText_(painter, -corner_ - text.length() - width_ / 200.0 - 5.0, -corner_ + (2.0 * grid_intensity_[0][i]), -near_ - 2 * corner_, text);
        }
        break;

      case PlotCanvas::IM_NONE:
      case PlotCanvas::IM_SNAP:
        int expo = 0;
        if (grid_intensity_.size() >= 1)
        {
          expo = (int)ceil(log10(grid_intensity_[0][0]));
        }
        if (grid_intensity_.size() >= 2)
        {
          if (expo >= ceil(log10(grid_intensity_[1][0])))
          {
            expo = (int)ceil(log10(grid_intensity_[1][0]));
          }
        }
        if (grid_intensity_.size() >= 3)
        {
          if (expo >= ceil(log10(grid_intensity_[2][0])))
          {
            expo = (int) ceil(log10(grid_intensity_[2][0]));
          }
        }

        if (canvas_3d_.legend_shown_)
        {
          font.setPixelSize(12);
          painter.setFont(font);
          text = QString("intensity e+%1").arg((double)expo, 0, 'f', 1);
          renderText_(painter, -corner_ - 20.0, corner_ + 10.0, -near_ - 2 * corner_ + 20.0, text);
          font.setPixelSize(10);
          painter.setFont(font);
        }

        if (zoom_ < 3.0 && grid_intensity_.size() >= 2)
        {
          for (Size i = 0; i < grid_intensity_[0].size(); i++)
          {
            double intensity = (double)grid_intensity_[0][i] / pow(10.0, expo);
            text = QString("%1").arg(intensity, 0, 'f', 1);
            renderText_(painter, -corner_ - text.length() - width_ / 200.0 - 5.0, -corner_ + scaledIntensity_(grid_intensity_[0][i], canvas_3d_.layers_.getCurrentLayerIndex()), -near_ - 2 * corner_, text);
          }
          for (Size i = 0; i < grid_intensity_[1].size(); i++)
          {
            double intensity = (double)grid_intensity_[1][i] / pow(10.0, expo);
            text = QString("%1").arg(intensity, 0, 'f', 1);
            renderText_(painter, -corner_ - text.length() - width_ / 200.0 - 5.0, -corner_ + scaledIntensity_(grid_intensity_[1][i], canvas_3d_.layers_.getCurrentLayerIndex()), -near_ - 2 * corner_, text);
          }
        }
        if (width_ > 800 && height_ > 600 && zoom_ < 2.0 && grid_intensity_.size() >= 3)
        {
          for (Size i = 0; i < grid_intensity_[2].size(); i++)
          {
            double intensity = (double)grid_intensity_[2][i] / pow(10.0, expo);
            text = QString("%1").arg(intensity, 0, 'f', 1);
            renderText_(painter, -corner_ - text.length() - width_ / 200.0 - 5.0, -corner_ + scaledIntensity_(grid_intensity_[2][i], canvas_3d_.layers_.getCurrentLayerIndex()), -near_ - 2 * corner_, text);
          }
        }
        break;

      }
    }
  }

  Plot3DRhiCanvas::VertexBatch Plot3DRhiCanvas::makeGround_()
  {
    VertexBatch batch;
    batch.topology = Topology::TRIANGLES;
    QColor color(toQString(std::string(canvas_3d_.param_.getValue("background_color").toString())));
    // the former quad, as two triangles
    const double y = -corner_ - 2.0;
    const double z_near = -near_ - 2 * corner_;
    const double z_far = -far_ + 2 * corner_;
    batch.add(-corner_, y, z_near, color);
    batch.add(-corner_, y, z_far, color);
    batch.add(corner_, y, z_far, color);
    batch.add(-corner_, y, z_near, color);
    batch.add(corner_, y, z_far, color);
    batch.add(corner_, y, z_near, color);
    return batch;
  }

  Plot3DRhiCanvas::VertexBatch Plot3DRhiCanvas::makeAxes_()
  {
    VertexBatch batch;
    batch.line_width = 3.0f;
    const QColor black(Qt::black);
    // x
    batch.add(-corner_, -corner_, -near_ - 2 * corner_, black);
    batch.add(corner_, -corner_, -near_ - 2 * corner_, black);
    // z
    batch.add(-corner_, -corner_, -near_ - 2 * corner_, black);
    batch.add(-corner_, -corner_, -far_ + 2 * corner_, black);
    // y
    batch.add(-corner_, -corner_, -near_ - 2 * corner_, black);
    batch.add(-corner_, corner_, -near_ - 2 * corner_, black);
    return batch;
  }

  Plot3DRhiCanvas::VertexBatch Plot3DRhiCanvas::makeDataAsTopView_()
  {
    VertexBatch batch;
    batch.topology = Topology::POINTS;

    for (Size i = 0; i < canvas_3d_.getLayerCount(); ++i)
    {
      const LayerDataPeak& layer = dynamic_cast<LayerDataPeak&>(canvas_3d_.getLayer(i));
      if (layer.visible)
      {
        const auto area = canvas_3d_.visible_area_.getAreaUnit();
        const MSExperiment& peak_data = layer.getPeakData()->getMSExperiment();
        auto begin_it = peak_data.areaBeginConst(area.getMinRT(), area.getMaxRT(), area.getMinMZ(), area.getMaxMZ());
        auto end_it = peak_data.areaEndConst();

        // count peaks in area
        int count = std::distance(begin_it, end_it);

        int max_displayed_peaks = 10000;
        int step = 1;
        if (count > max_displayed_peaks)
        {
          step = 1 + count / max_displayed_peaks;
        }

        for (auto it = begin_it; it != end_it; ++it)
        {
          if (step > 1)
          {
            for (int j = 0; j < step - 1; ++j)
            {
              ++it;
            }
          }
          if (it == end_it)
          {
            return batch;
          }

          PeakIndex pi = it.getPeakIndex();
          if (layer.filters.passes(peak_data[pi.spectrum], pi.peak))
          {
            QColor color;
            switch (canvas_3d_.intensity_mode_)
            {
            case PlotCanvas::IM_NONE:
              color = layer.gradient.precalculatedColorAt(it->getIntensity());
              break;

            case PlotCanvas::IM_PERCENTAGE:
              color = layer.gradient.precalculatedColorAt(it->getIntensity() * 100.0 / canvas_3d_.getMaxIntensity(i));
              break;

            case PlotCanvas::IM_SNAP:
              color = layer.gradient.precalculatedColorAt(it->getIntensity());
              break;

            case PlotCanvas::IM_LOG:
              color = layer.gradient.precalculatedColorAt(log10(1 + max(0.0, (double)(it->getIntensity()))));
              break;
            }
            batch.add(-corner_ + scaledMZ_(it->getMZ()),
                      -corner_,
                      -near_ - 2 * corner_ - scaledRT_(it.getRT()),
                      color);
          }
        }
      }
    }
    return batch;
  }

  Plot3DRhiCanvas::VertexBatch Plot3DRhiCanvas::makeDataAsStick_()
  {
    VertexBatch batch;

    for (Size i = 0; i < canvas_3d_.getLayerCount(); i++)
    {
      LayerDataPeak& layer = dynamic_cast<LayerDataPeak&>(canvas_3d_.getLayer(i));
      if (layer.visible)
      {
        recalculateDotGradient_(layer);

        // Flat shading painted the whole stick in the colour of its last vertex;
        // smooth shading blends from the base colour to the intensity colour.
        const bool smooth = (Int)layer.param.getValue("dot:shade_mode") != 0;
        batch.line_width = float((double)layer.param.getValue("dot:line_width"));

        const auto area = canvas_3d_.visible_area_.getAreaUnit();
        const MSExperiment& peak_data = layer.getPeakData()->getMSExperiment();
        auto begin_it = peak_data.areaBeginConst(area.getMinRT(), area.getMaxRT(), area.getMinMZ(), area.getMaxMZ());
        auto end_it = peak_data.areaEndConst();
        // count peaks in area
        int count = std::distance(begin_it, end_it);

        int max_displayed_peaks = 100000;
        int step = 1;
        if (count > max_displayed_peaks)
        {
          step = 1 + count / max_displayed_peaks;
        }

        for (auto it = begin_it; it != end_it; ++it)
        {
          if (step > 1)
          {
            for (int j = 0; j < step - 1; ++j)
            {
              ++it;
            }
          }
          if (it == end_it)
          {
            return batch;
          }

          PeakIndex pi = it.getPeakIndex();
          if (layer.filters.passes(peak_data[pi.spectrum], pi.peak))
          {
            QColor top;
            switch (canvas_3d_.intensity_mode_)
            {
            case PlotCanvas::IM_PERCENTAGE:
              top = layer.gradient.precalculatedColorAt(it->getIntensity() * 100.0 / canvas_3d_.getMaxIntensity(i));
              break;

            case PlotCanvas::IM_NONE:
            case PlotCanvas::IM_SNAP:
              top = layer.gradient.precalculatedColorAt(it->getIntensity());
              break;

            case PlotCanvas::IM_LOG:
              top = layer.gradient.precalculatedColorAt(log10(1 + max(0.0, (double)(it->getIntensity()))));
              break;
            }
            const QColor base = smooth ? layer.gradient.precalculatedColorAt(0.0) : top;
            const double x = -corner_ + scaledMZ_(it->getMZ());
            const double z = -near_ - 2 * corner_ - scaledRT_(it.getRT());
            batch.add(x, -corner_, z, base);
            batch.add(x, -corner_ + scaledIntensity_(it->getIntensity(), i), z, top);
          }
        }
      }
    }
    return batch;
  }

  Plot3DRhiCanvas::VertexBatch Plot3DRhiCanvas::makeGridLines_()
  {
    VertexBatch batch;
    // The former stippled lines keep their translucency; modern APIs offer no stipple.
    const QColor color(0, 0, 0, 80);
    // mz
    for (Size level = 0; level < grid_mz_.size() && level < 3; ++level)
    {
      for (Size i = 0; i < grid_mz_[level].size(); i++)
      {
        batch.add(-corner_ + scaledMZ_(grid_mz_[level][i]), -corner_, -near_ - 2 * corner_, color);
        batch.add(-corner_ + scaledMZ_(grid_mz_[level][i]), -corner_, -far_ + 2 * corner_, color);
      }
    }
    // rt
    for (Size level = 0; level < grid_rt_.size() && level < 3; ++level)
    {
      for (Size i = 0; i < grid_rt_[level].size(); i++)
      {
        batch.add(-corner_, -corner_, -near_ - 2 * corner_ - scaledRT_(grid_rt_[level][i]), color);
        batch.add(corner_, -corner_, -near_ - 2 * corner_ - scaledRT_(grid_rt_[level][i]), color);
      }
    }
    return batch;
  }

  Plot3DRhiCanvas::VertexBatch Plot3DRhiCanvas::makeAxesTicks_()
  {
    VertexBatch batch;
    batch.line_width = 2.0f;
    const QColor black(Qt::black);
    // tick length shrinks with each finer grid level: 4, 3, 2
    const double lengths[3] = {4.0, 3.0, 2.0};

    // mz
    for (Size level = 0; level < grid_mz_.size() && level < 3; ++level)
    {
      for (Size i = 0; i < grid_mz_[level].size(); i++)
      {
        batch.add(-corner_ + scaledMZ_(grid_mz_[level][i]), -corner_, -near_ - 2 * corner_, black);
        batch.add(-corner_ + scaledMZ_(grid_mz_[level][i]), -corner_ + lengths[level], -near_ - 2 * corner_, black);
      }
    }

    // rt
    for (Size level = 0; level < grid_rt_.size() && level < 3; ++level)
    {
      for (Size i = 0; i < grid_rt_[level].size(); i++)
      {
        batch.add(-corner_, -corner_, -near_ - 2 * corner_ - scaledRT_(grid_rt_[level][i]), black);
        batch.add(-corner_, -corner_ + lengths[level], -near_ - 2 * corner_ - scaledRT_(grid_rt_[level][i]), black);
      }
    }

    //Intensity
    const Size current = canvas_3d_.layers_.getCurrentLayerIndex();
    switch (canvas_3d_.intensity_mode_)
    {
    case PlotCanvas::IM_PERCENTAGE:
      if (grid_intensity_.size() >= 1)
      {
        for (Size i = 0; i < grid_intensity_[0].size(); i++)
        {
          batch.add(-corner_, -corner_ + (2.0 * grid_intensity_[0][i]), -near_ - 2 * corner_, black);
          batch.add(-corner_ + 4.0, -corner_ + (2.0 * grid_intensity_[0][i]), -near_ - 2 * corner_ - 4.0, black);
        }
      }
      break;

    case PlotCanvas::IM_NONE:
    case PlotCanvas::IM_SNAP:
      for (Size level = 0; level < grid_intensity_.size() && level < 3; ++level)
      {
        for (Size i = 0; i < grid_intensity_[level].size(); i++)
        {
          const double y = -corner_ + scaledIntensity_(grid_intensity_[level][i], current);
          batch.add(-corner_, y, -near_ - 2 * corner_, black);
          batch.add(-corner_ + lengths[level], y, -near_ - 2 * corner_ - lengths[level], black);
        }
      }
      break;

    case PlotCanvas::IM_LOG:
      if (grid_intensity_.size())
      {
        for (Size i = 0; i < grid_intensity_[0].size(); i++)
        {
          const double y = -corner_ + scaledIntensity_(pow(10.0, grid_intensity_[0][i]) - 1, current);
          batch.add(-corner_, y, -near_ - 2 * corner_, black);
          batch.add(-corner_ + 4.0, y, -near_ - 2 * corner_ - 4.0, black);
        }
      }
      break;

    }
    return batch;
  }

  double Plot3DRhiCanvas::scaledRT_(double rt)
  {
    double scaledrt = rt - canvas_3d_.visible_area_.getAreaUnit().getMinRT();
    scaledrt = scaledrt * 2.0 * corner_ / canvas_3d_.visible_area_.getAreaUnit().RangeRT::getSpan();
    return scaledrt;
  }

  double Plot3DRhiCanvas::scaledInversRT_(double rt)
  {
    double i_rt = rt * canvas_3d_.visible_area_.getAreaUnit().RangeRT::getSpan();
    i_rt = i_rt / 200.0;
    i_rt = i_rt + canvas_3d_.visible_area_.getAreaUnit().getMinRT();
    return i_rt;
  }

  double Plot3DRhiCanvas::scaledMZ_(double mz)
  {
    double scaledmz = mz - canvas_3d_.visible_area_.getAreaUnit().getMinMZ();
    scaledmz = scaledmz * 2.0 * corner_ / canvas_3d_.visible_area_.getAreaUnit().RangeMZ::getSpan();
    return scaledmz;
  }

  double Plot3DRhiCanvas::scaledInversMZ_(double mz)
  {
    double i_mz = mz * canvas_3d_.visible_area_.getAreaUnit().RangeMZ::getSpan();
    i_mz = i_mz / 200;
    i_mz = i_mz + canvas_3d_.visible_area_.getAreaUnit().getMinMZ();
    return i_mz;
  }

  double Plot3DRhiCanvas::scaledIntensity_(float intensity, Size layer_index)
  {
    double scaledintensity = intensity * 2.0 * corner_;
    switch (canvas_3d_.intensity_mode_)
    {
    case PlotCanvas::IM_SNAP:
      scaledintensity /= int_scale_.max_[0];
      break;

    case PlotCanvas::IM_NONE:
      scaledintensity /= canvas_3d_.overall_data_range_.getMaxIntensity();
      break;

    case PlotCanvas::IM_PERCENTAGE:
      scaledintensity /= canvas_3d_.getMaxIntensity(layer_index);
      break;

    case PlotCanvas::IM_LOG:
      scaledintensity = log10(1 + max(0.0, (double)intensity)) * 2.0 * corner_ / log10(1 + max(0.0, canvas_3d_.overall_data_range_.getMaxIntensity()));
      break;
    }
    return scaledintensity;
  }

  void Plot3DRhiCanvas::normalizeAngle(int* angle)
  {
    while (*angle < 0) { *angle += 360 * 16; }
    while (*angle > 360 * 16) { *angle -= 360 * 16; }
  }

  ///////////////wheel- and MouseEvents//////////////////

  void Plot3DRhiCanvas::actionModeChange()
  {
    //change from translate to zoom
    if (canvas_3d_.action_mode_ == PlotCanvas::AM_ZOOM)
    {
      storeRotationAndZoom();
      xrot_ = 220;
      yrot_ = 220;
      zrot_ = 0;
      canvas_3d_.update_buffer_ = true;
      canvas_3d_.update_(OPENMS_PRETTY_FUNCTION);
    }
    //change from zoom to translate
    else if (canvas_3d_.action_mode_ == PlotCanvas::AM_TRANSLATE)
    {
      // if still in selection mode, quit selection mode first:
      if (canvas_3d_.rubber_band_.isVisible())
      {
        computeSelection_();
      }
      restoreRotationAndZoom();
      canvas_3d_.update_buffer_ = true;
      canvas_3d_.update_(OPENMS_PRETTY_FUNCTION);
    }
    update();
  }

  void Plot3DRhiCanvas::focusOutEvent(QFocusEvent * e)
  {
    canvas_3d_.focusOutEvent(e);
    update();
  }

  void Plot3DRhiCanvas::mousePressEvent(QMouseEvent * e)
  {
    mouse_move_begin_ = e->pos();
    mouse_move_end_ = e->pos();

    if (canvas_3d_.action_mode_ == PlotCanvas::AM_ZOOM && e->button() == Qt::LeftButton)
    {
      canvas_3d_.rubber_band_.setGeometry(QRect(e->pos(), QSize()));
      canvas_3d_.rubber_band_.show();
      canvas_3d_.update_buffer_ = true;
      canvas_3d_.update_(OPENMS_PRETTY_FUNCTION);
    }
    update();
  }

  void Plot3DRhiCanvas::mouseMoveEvent(QMouseEvent * e)
  {
    if (e->buttons() & Qt::LeftButton)
    {
      if (canvas_3d_.action_mode_ == PlotCanvas::AM_ZOOM)
      {
        canvas_3d_.rubber_band_.setGeometry(QRect(mouse_move_begin_, e->pos()).normalized());
        canvas_3d_.update_(OPENMS_PRETTY_FUNCTION);
      }
      else if (canvas_3d_.action_mode_ == PlotCanvas::AM_TRANSLATE)
      {
        Int x_angle = xrot_ + 8 * (e->position().y() - mouse_move_end_.y());
        normalizeAngle(&x_angle);
        xrot_ = x_angle;

        Int y_angle = yrot_ + 8 * (e->position().x() - mouse_move_end_.x());
        normalizeAngle(&y_angle);
        yrot_ = y_angle;

        mouse_move_end_ = e->pos();
        canvas_3d_.update_(OPENMS_PRETTY_FUNCTION);
      }
    }
    update();
  }

  void Plot3DRhiCanvas::mouseReleaseEvent(QMouseEvent * e)
  {
    if (canvas_3d_.action_mode_ == PlotCanvas::AM_ZOOM && e->button() == Qt::LeftButton)
    {
      computeSelection_();
    }
    update();
  }

  void Plot3DRhiCanvas::computeSelection_()
  {
    QRect rect = canvas_3d_.rubber_band_.geometry();
    x_1_ = ((rect.topLeft().x() - width_ / 2) * corner_ * 1.25 * 2) / width_;
    y_1_ = -300 + (((rect.topLeft().y() - height_ / 2) * corner_ * 1.25 * 2) / height_);
    x_2_ = ((rect.bottomRight().x() - width_ / 2) * corner_ * 1.25 * 2) / width_;
    y_2_ = -300 + (((rect.bottomRight().y() - height_ / 2) * corner_ * 1.25 * 2) / height_);
    dataToZoomArray_(x_1_, y_1_, x_2_, y_2_);
    canvas_3d_.rubber_band_.hide();
    canvas_3d_.update_buffer_ = true;
    canvas_3d_.update_(OPENMS_PRETTY_FUNCTION);
  }

  void Plot3DRhiCanvas::dataToZoomArray_(double x_1, double y_1, double x_2, double y_2)
  {
    double scale_x1 = scaledInversRT_(-200 - y_1);
    double scale_x2 = scaledInversRT_(-200 - y_2);
    double scale_y1 = scaledInversMZ_(x_1 + 100.0);
    double scale_y2 = scaledInversMZ_(x_2 + 100.0);
    DRange<2> new_area_;
    if (scale_x1 > scale_x2)
    {
      std::swap(scale_x1, scale_x2);
    }
    new_area_.min_[0] = scale_x1;
    new_area_.max_[0] = scale_x2;

    if (scale_y1 > scale_y2)
    {
      std::swap(scale_y1, scale_y2);
    }
    new_area_.min_[1] = scale_y1;
    new_area_.max_[1] = scale_y2;
    canvas_3d_.changeVisibleArea_(canvas_3d_.visible_area_.cloneWith(new_area_), true, true);
  }

  void Plot3DRhiCanvas::updateIntensityScale()
  {
    int_scale_.min_[0] = canvas_3d_.overall_data_range_.getMaxIntensity();
    int_scale_.max_[0] = canvas_3d_.overall_data_range_.getMinIntensity();

    const auto area = canvas_3d_.visible_area_.getAreaUnit();
    for (Size i = 0; i < canvas_3d_.getLayerCount(); i++)
    {
      const auto& layer = dynamic_cast<const LayerDataPeak&>(canvas_3d_.getLayer(i));
      const MSExperiment& peak_data = layer.getPeakData()->getMSExperiment();
      auto rt_begin_it = peak_data.RTBegin(area.getMinRT());
      auto rt_end_it = peak_data.RTEnd(area.getMaxRT());

      for (auto spec_it = rt_begin_it; spec_it != rt_end_it; ++spec_it)
      {
        auto mz_end = spec_it->MZEnd(area.getMaxMZ());
        for (auto it = spec_it->MZBegin(area.getMinMZ()); it != mz_end; ++it)
        {
          Math::extendRange(int_scale_.min_[0], int_scale_.max_[0], (double)it->getIntensity());
        }
      }
    }
  }

  void Plot3DRhiCanvas::recalculateDotGradient_(LayerDataBase& layer)
  {
    layer.gradient.fromString(layer.param.getValue("dot:gradient"));
    switch (canvas_3d_.intensity_mode_)
    {
    case PlotCanvas::IM_SNAP:
      layer.gradient.activatePrecalculationMode(0.0, int_scale_.max_[0], UInt(canvas_3d_.param_.getValue("dot:interpolation_steps")));
      break;

    case PlotCanvas::IM_NONE:
      layer.gradient.activatePrecalculationMode(0.0, canvas_3d_.overall_data_range_.getMaxIntensity(), UInt(canvas_3d_.param_.getValue("dot:interpolation_steps")));
      break;

    case PlotCanvas::IM_PERCENTAGE:
      layer.gradient.activatePrecalculationMode(0.0, 100.0, UInt(canvas_3d_.param_.getValue("dot:interpolation_steps")));
      break;

    case PlotCanvas::IM_LOG:
      layer.gradient.activatePrecalculationMode(0.0, log10(1 + max(0.0, canvas_3d_.overall_data_range_.getMaxIntensity())), UInt(canvas_3d_.param_.getValue("dot:interpolation_steps")));
      break;
    }
  }

} //end of namespace
