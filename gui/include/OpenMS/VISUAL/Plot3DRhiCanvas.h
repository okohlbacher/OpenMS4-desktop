// Copyright (c) 2002-present, OpenMS Inc. -- EKU Tuebingen, ETH Zurich, and FU Berlin
// SPDX-License-Identifier: BSD-3-Clause
//
// --------------------------------------------------------------------------
// $Maintainer: Timo Sachsenberg $
// $Authors: Cornelia Friedle $
// --------------------------------------------------------------------------

#pragma once

// OpenMS_GUI config
#include <OpenMS/VISUAL/OpenMS_GUIConfig.h>

#include <QMatrix4x4>
#include <QRhiWidget>

// OpenMS
#include <OpenMS/DATASTRUCTURES/DRange.h>

#include <memory>
#include <vector>

class QPainter;

namespace OpenMS
{
  class Plot3DCanvas;
  class LayerDataBase;

  namespace Internal
  {
    class Plot3DLegendOverlay;
  }

  /**
      @brief QRhi canvas for 3D-visualization of map data

      Renders the 3D peak map through Qt's rendering hardware interface, so the
      same code runs on Metal, Direct3D, Vulkan and OpenGL. Geometry is built on
      the CPU as vertex batches and uploaded when the data or view mode changes;
      the axis legend is painted by a transparent overlay widget, because a
      QRhiWidget composites a texture and cannot be painted on directly.

      @note Do not use this class directly. Use Plot3DCanvas instead!

      @ingroup PlotWidgets
  */
  class OPENMS_GUI_DLLAPI Plot3DRhiCanvas :
    public QRhiWidget
  {
    Q_OBJECT

    friend class Plot3DCanvas;
    friend class Internal::Plot3DLegendOverlay;

public:

    /// Container for axis ticks
    typedef std::vector<std::vector<double> > AxisTickVector;

    /**
     @brief Constructor

     @param[in] parent The parent widget
     @param[in] canvas_3d The main 3d canvas
    */
    Plot3DRhiCanvas(QWidget * parent, Plot3DCanvas & canvas_3d);
    /**
        @brief Destructor

        Destroys the widget, its GPU resources and all associated data.
    */
    ~Plot3DRhiCanvas() override;

    /** @name Reimplemented QRhiWidget rendering */
    //@{
    void initialize(QRhiCommandBuffer * cb) override;
    void render(QRhiCommandBuffer * cb) override;
    //@}

    /** @name Reimplemented QT events */
    //@{
    void resizeEvent(QResizeEvent * e) override;
    void mouseMoveEvent(QMouseEvent * e) override;
    void mouseReleaseEvent(QMouseEvent * e) override;
    void mousePressEvent(QMouseEvent * e) override;
    void focusOutEvent(QFocusEvent * e) override;
    //@}

    void setXLabel(const QString& l) { x_label_ = l; }
    void setYLabel(const QString& l) { y_label_ = l; }
    void setZLabel(const QString& l) { z_label_ = l; }

    /// updates the min and max values of the intensity
    void updateIntensityScale();

    /// Rebuilds all geometry from the canvas data before the next frame
    void markGeometryDirty();

    /// Number of vertices in each drawable group of the current view
    struct GeometryVertexCounts
    {
      int ground = 0;
      int axes = 0;
      int axes_ticks = 0;
      int gridlines = 0;
      int stickdata = 0;
    };

    /// Rebuilds all geometry from the canvas data now, without needing a frame
    void rebuildGeometry();

    /// Reports how many vertices the last geometry build produced
    GeometryVertexCounts geometryVertexCounts() const;

protected:
    /// Which primitive a vertex batch draws
    enum class Topology
    {
      TRIANGLES,
      LINES,
      POINTS
    };

    /// CPU-side vertices of one drawable group: xyz position and rgba colour, interleaved
    struct VertexBatch
    {
      std::vector<float> data;
      Topology topology = Topology::LINES;
      float line_width = 1.0f;

      /// appends one vertex
      void add(double x, double y, double z, const QColor& color);
      /// number of vertices in the batch
      int vertexCount() const { return int(data.size() / 7); }
    };

    /// helper function to project point to device space
    bool project_(double objx, double objy, double objz, double * winx, double * winy) const;
    /// renders text at the projected position of a world coordinate
    void renderText_(QPainter& painter, double x, double y, double z, const QString & text) const;
    /// Builds the geometry for the peak sticks of the 3D view
    VertexBatch makeDataAsStick_();
    /// Builds the geometry for the axes
    VertexBatch makeAxes_();
    /// Builds the geometry for axis ticks
    VertexBatch makeAxesTicks_();
    /// Builds the geometry for the birds-eye view
    VertexBatch makeDataAsTopView_();
    /// Builds the geometry for the background
    VertexBatch makeGround_();
    /// Builds the geometry for grid lines
    VertexBatch makeGridLines_();
    /// Draws the axis texts
    void drawAxesLegend_(QPainter& painter);

    /// Rebuilds the vertex batches according to the current action mode
    void rebuildGeometry_();
    /// Recomputes the projection and model-view matrices for the current view
    void updateMatrices_();

    /// computes the dataset supposed to be drawn when a section has been selected in zoom mode
    void computeSelection_();

    /// calculates the zoom area , which is shown
    void dataToZoomArray_(double x_1, double y_1, double x_2, double y_2);

    /// returns the BB-rt-coordinate :  value --> BB-coordinates
    double scaledRT_(double rt);
    /// returns the rt-value : BB-coordinates  --> value
    double scaledInversRT_(double mz);
    /// returns the BB-mz-coordinate :  values --> BB-coordinates
    double scaledMZ_(double mz);
    ///  returns the mz-value : BB-coordinates  --> value
    double scaledInversMZ_(double mz);
    /// returns the BB-intensity -coordinate :  values --> BB-coordinates
    double scaledIntensity_(float intensity, Size layer_index);

    /// recalculates the dot gradient interpolation values.
    void recalculateDotGradient_(LayerDataBase& layer);
    ///calculate the ticks for the gridlines
    void calculateGridLines_();

    /// normalize the angle by "angle % 360*16"
    void normalizeAngle(int* angle);
    // set translation vector to 0
    void resetTranslation();

    /// stores the original rotation and zoom factor (e.g. before changing into zoom mode)
    void storeRotationAndZoom();
    /// restores the original rotation and zoom factor (e.g. before changing into zoom mode)
    void restoreRotationAndZoom();

    /** @name Vertex batches of the current view */
    //@{
    VertexBatch stickdata_;
    VertexBatch axes_;
    VertexBatch axes_ticks_;
    VertexBatch gridlines_;
    VertexBatch ground_;
    //@}

    /// GPU resources; only the implementation knows the QRhi types
    struct RhiState;
    std::unique_ptr<RhiState> rhi_state_;

    /// whether the vertex batches must be rebuilt before the next frame
    bool geometry_dirty_ = true;
    /// whether the GPU buffers must be re-uploaded before the next frame
    bool upload_dirty_ = true;

    /// projection of the current view, in OpenGL clip-space conventions
    QMatrix4x4 projection_;
    /// model-view transform of the current view
    QMatrix4x4 modelview_;

    /// paints the axis legend over the rendered texture
    Internal::Plot3DLegendOverlay* legend_overlay_ = nullptr;

    /// reference to Plot3DCanvas
    Plot3DCanvas & canvas_3d_;

    /// member x-variables for the rotation
    int xrot_;
    /// member y-variables for the rotation
    int yrot_;
    /// member z-variables for the rotation
    int zrot_;

    /// member x-variable that stores the original angle during zoom mode
    int xrot_tmp_;
    /// member y-variable that stores the original angle during zoom mode
    int yrot_tmp_;
    /// member z-variable that stores the original angle during zoom mode
    int zrot_tmp_;

    /// member variables for the zoom-mode
    QPoint mouse_move_end_, mouse_move_begin_;

    ///member variable for the x and y axis of the BB
    double corner_;
    /// member variable for the zoom mode
    double zoom_;
    /// member variable that stores original zoom factor during zoom mode
    double zoom_tmp_;

    /// member variable for the z- axis of the BB
    double near_;
    /// member variable for the z- axis of the BB
    double far_;
    /// the width of the viewport
    float width_;
    /// the height of the viewport
    float height_;
    /// object which contains the min and max values of mz, rt and intensity
    DRange<3> overall_values_;
    ///object which contains the values of the current min and max intensity
    DRange<1> int_scale_;
    ///member gridvectors which contains the data for the mz-axis-ticks
    AxisTickVector grid_mz_;
    ///member gridvectors which contains the data for the rt-axis-ticks
    AxisTickVector grid_rt_;
    ///member gridvectors which contains the data for the intensity-axis-ticks
    AxisTickVector grid_intensity_;
    /// x1 coordinate of the zoomselection
    double x_1_;
    /// x2 coordinate of the zoomselection
    double x_2_;
    /// y1 coordinate of the zoomselection
    double y_1_;
    /// y2 coordinate of the zoomselection
    double y_2_;
    /// x- translation
    double trans_x_;
    /// y_translation
    double trans_y_;

    QString x_label_;
    QString y_label_;
    QString z_label_;

protected slots:
    /// Slot that reacts on action mode changes
    void actionModeChange();
  };
}
