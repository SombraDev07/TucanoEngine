---
title: Vector shapes
---

Vector shapes allow you to define scalable, resolution-independent graphics that can be rendered at any size without loss of quality. The framework provides the @b3d::VectorPath class for defining vector graphics, and the @b3d::SpriteVectorPath class for using them as renderable sprites. For more information about sprites in general, see the [sprite images](00_spriteImages.md) manual.

# VectorPath

The @b3d::VectorPath class represents a collection of curves and geometric shapes that can be rasterized to any dimension. Paths are defined in a coordinate system called the canvas, which determines the logical size of your drawing area.

Create a vector path by calling @b3d::VectorPath::Create and optionally specifying a canvas size:

~~~~~~~~~~~~~{.cpp}
Size2 canvasSize(512.0f, 512.0f);
HVectorPath vectorPath = VectorPath::Create(canvasSize);
~~~~~~~~~~~~~

If you don't specify a canvas size, it defaults to 512x512 units. Units used here are arbitrary, as the shape can be scaled to any size. However you need to make sure all your draw operations are within the canvas bounds.

## Drawing shapes

**VectorPath** provides methods for drawing various geometric primitives. All drawing happens in the canvas coordinate system.

### Basic shapes

Draw simple geometric shapes directly:

~~~~~~~~~~~~~{.cpp}
HVectorPath vectorPath = VectorPath::Create();

// Draw a rectangle
vectorPath->DrawRectangle(Area2(10.0f, 10.0f, 100.0f, 50.0f));

// Draw a rounded rectangle with uniform corner radius
vectorPath->DrawRoundedRectangle(Area2(120.0f, 10.0f, 100.0f, 50.0f), 10.0f);

// Draw a circle
vectorPath->DrawCircle(Vector2(250.0f, 250.0f), 50.0f);

// Draw an ellipse
vectorPath->DrawEllipse(Vector2(350.0f, 250.0f), Vector2(60.0f, 40.0f));

// Draw an arc
vectorPath->DrawArc(Vector2(450.0f, 250.0f), 50.0f, Degree(0.0f), Degree(270.0f));
~~~~~~~~~~~~~

### Path-based drawing

For more complex shapes, you can build custom paths using line segments and curves. The path uses a drawing cursor that tracks the current position.

Set the cursor position with @b3d::VectorPath::SetDrawCursor:

~~~~~~~~~~~~~{.cpp}
vectorPath->SetDrawCursor(Vector2(50.0f, 50.0f));
~~~~~~~~~~~~~

Draw lines to create connected segments:

~~~~~~~~~~~~~{.cpp}
vectorPath->SetDrawCursor(Vector2(50.0f, 50.0f));
vectorPath->DrawLineTo(Vector2(150.0f, 50.0f));
vectorPath->DrawLineTo(Vector2(150.0f, 150.0f));
vectorPath->DrawLineTo(Vector2(50.0f, 150.0f));
vectorPath->ClosePath(); // Connect back to the starting point
~~~~~~~~~~~~~

Draw curves for smooth shapes:

~~~~~~~~~~~~~{.cpp}
vectorPath->SetDrawCursor(Vector2(50.0f, 150.0f));

// Quadratic bezier curve
Vector2 controlPoint(100.0f, 50.0f);
Vector2 endPoint(150.0f, 150.0f);
vectorPath->DrawQuadraticBezierTo(controlPoint, endPoint);

// Cubic bezier curve
Vector2 controlPoint1(200.0f, 50.0f);
Vector2 controlPoint2(250.0f, 200.0f);
Vector2 curveEndPoint(300.0f, 150.0f);
vectorPath->DrawCubicBezierTo(controlPoint1, controlPoint2, curveEndPoint);

// Arc using three points
Vector2 middlePoint(400.0f, 100.0f);
Vector2 arcEndPoint(450.0f, 150.0f);
float radius = 50.0f;
vectorPath->DrawArcTo(middlePoint, arcEndPoint, radius);
~~~~~~~~~~~~~

## Styling shapes

Before rendering a shape, you need to configure its visual appearance using fill and stroke properties.

### Fill and stroke

Set fill and stroke paints using @b3d::VectorPath::SetFillPaint and @b3d::VectorPath::SetStrokePaint. You can use solid colors or gradients:

~~~~~~~~~~~~~{.cpp}
// Solid color fill
VectorGraphicsPaint fillPaint = VectorGraphicsPaint::CreateSolid(Color::kRed);
vectorPath->SetFillPaint(fillPaint);

// Solid color stroke
VectorGraphicsPaint strokePaint = VectorGraphicsPaint::CreateSolid(Color::kBlack);
vectorPath->SetStrokePaint(strokePaint);
~~~~~~~~~~~~~

Configure stroke appearance:

~~~~~~~~~~~~~{.cpp}
vectorPath->SetStrokeWidth(2.0f);
vectorPath->SetLineCapType(VectorGraphicsLineCapType::Round);
vectorPath->SetLineJoinType(VectorGraphicsLineJoinStyle::Miter);
vectorPath->SetMiterLimit(10.0f);
~~~~~~~~~~~~~

### Gradients

Create gradient paints for more sophisticated visual effects:

~~~~~~~~~~~~~{.cpp}
// Linear gradient
VectorGraphicsPaint linearGradient = VectorGraphicsPaint::CreateLinearGradient(
    Color::kRed,        // Start color
    Color::kBlue,       // End color
    Vector2(0.0f, 0.0f),    // Start point
    Vector2(100.0f, 0.0f)   // End point
);

// Box gradient (color at center fading to another at edges)
VectorGraphicsPaint boxGradient = VectorGraphicsPaint::CreateBoxGradient(
    Color::kWhite,      // Inner color
    Color::kBlack,      // Outer color
    Area2(10.0f, 10.0f, 100.0f, 100.0f), // Box area
    5.0f,               // Corner radius
    10.0f               // Feather amount
);

// Radial gradient
VectorGraphicsPaint radialGradient = VectorGraphicsPaint::CreateRadialGradient(
    Color::kYellow,     // Inner color
    Color::kRed,        // Outer color
    Vector2(50.0f, 50.0f),  // Center
    0.0f,               // Inner radius
    50.0f               // Outer radius
);
~~~~~~~~~~~~~

### Rendering shapes

After defining a shape, render it by calling @b3d::VectorPath::DrawFill or @b3d::VectorPath::DrawStroke:

~~~~~~~~~~~~~{.cpp}
vectorPath->DrawRectangle(Area2(10.0f, 10.0f, 100.0f, 50.0f));
vectorPath->DrawFill();   // Fill the rectangle
vectorPath->DrawStroke(); // Draw the rectangle outline
~~~~~~~~~~~~~

## Advanced features

### Creating holes

Create holes in shapes by setting the path solidity to Hole after recording a path:

~~~~~~~~~~~~~{.cpp}
// Draw outer rectangle
vectorPath->DrawRectangle(Area2(10.0f, 10.0f, 200.0f, 200.0f));
vectorPath->SetFillPaint(VectorGraphicsPaint::CreateSolid(Color::kRed));

// Draw inner rectangle as a hole
vectorPath->DrawRectangle(Area2(50.0f, 50.0f, 120.0f, 120.0f));
vectorPath->SetSolidity(VectorGraphicsPathSolidity::Hole);

vectorPath->DrawFill();
~~~~~~~~~~~~~

### Transparency and blending

Control shape opacity and how shapes blend with each other:

~~~~~~~~~~~~~{.cpp}
vectorPath->SetAlpha(0.5f); // 50% transparency
vectorPath->SetBlendMode(VectorGraphicsBlendMode::SourceOver);
~~~~~~~~~~~~~

### Clipping

Restrict rendering to a specific rectangular region:

~~~~~~~~~~~~~{.cpp}
vectorPath->SetScissorRectangle(Area2(0.0f, 0.0f, 256.0f, 256.0f));
// ... draw shapes ...
vectorPath->ClearScissor();
~~~~~~~~~~~~~

### Antialiasing

Control antialiasing for smoother or crisper rendering:

~~~~~~~~~~~~~{.cpp}
vectorPath->SetAntialiasShapes(true);
~~~~~~~~~~~~~

# SpriteVectorPath

Once you have created a **VectorPath**, you can use it as a sprite by creating a @b3d::SpriteVectorPath. This allows the vector path to be rasterized at different sizes and used with GUI elements, particle systems, and other rendering systems.

Create a sprite vector path by providing the vector path and a default rendering size:

~~~~~~~~~~~~~{.cpp}
HVectorPath vectorPath = VectorPath::Create();
// ... draw shapes ...

Size2I defaultSize(64, 64);
HSpriteVectorPath spriteVectorPath = SpriteVectorPath::Create(vectorPath, defaultSize);
~~~~~~~~~~~~~

The sprite vector path will automatically rasterize the vector graphics to a texture. When rendered at different scales or sizes, it can re-rasterize to maintain optimal quality.

You can control how the canvas is mapped to the rasterized size using the @b3d::SpriteVectorPathCreateInformation structure:

~~~~~~~~~~~~~{.cpp}
SpriteVectorPathCreateInformation createInformation;
createInformation.VectorPath = vectorPath;
createInformation.DefaultSize = Size2I(64, 64);
createInformation.ScalingMode = VectorGraphicsRasterizationScaling::ScaleToFit;

HSpriteVectorPath spriteVectorPath = SpriteVectorPath::Create(createInformation);
~~~~~~~~~~~~~

Available scaling modes include:
- **StretchToFit**: Canvas stretches non-uniformly to fill the entire raster area
- **ScaleToFit**: Canvas scales uniformly until one dimension fits, may leave empty space
- **CropToFit**: Canvas scales uniformly until both dimensions are covered, may crop content
- **None**: No scaling applied, canvas coordinates map 1:1 to raster coordinates

For more information about using sprite vector paths with other systems, see the [sprite images](00_spriteImages.md) manual.
