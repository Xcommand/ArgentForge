#pragma once

#include "Graphics/SImage/SImage.h"

namespace slade
{
class SAction;

class SBrush
{
public:
	// How big and how soft a brush is allowed to be, the same numbers whether the
	// size comes from the picker's sliders or from dragging on the canvas
	static constexpr int min_size     = 1;
	static constexpr int max_size     = 99;
	static constexpr int min_feather  = 0;
	static constexpr int max_feather  = 99;

	// The shapes a brush of any size can be made from
	enum class Shape
	{
		Square,
		Circle,
		Diamond,
	};

	SBrush(const string& name);
	~SBrush() = default;

	// SAction getAction(); // Returns an action ready to be inserted in a menu or toolbar (NYI)

	string  name() const { return name_; } // Returns the brush's name ("pgfx_brush_xyz")
	string  icon() const { return icon_; } // Returns the brush's icon name ("brush_xyz")
	uint8_t pixel(int x, int y) const;

	// How far the brush reaches out from its center, so painting knows how many
	// pixels to look at
	int radius() const { return image_ ? std::max(image_->width(), image_->height()) / 2 : 0; }

	// How many pixels across it is, which is the number the size control was set to
	int width() const { return image_ ? image_->width() : 0; }

	// Dither patterns are meant to be laid down edge to edge, so a stroke repeats
	// one every 8 pixels (the 9x9 tile sharing its edge) instead of at every
	// pixel it crosses. 0 for brushes that just follow the mouse
	int tileStep() const { return tiles_ ? 8 : 0; }

	static SBrush* get(const string& name);

	// The name a shape goes by in the settings, and the shape saved under [name]
	static const char* shapeName(Shape shape);
	static Shape       shapeFromName(const string& name);

	// The brush for [shape] as wide as [size] pixels, its edge fading out over the
	// last [feather] of them. One is kept around for every combination asked for,
	// since a canvas points at the brush it's using
	static SBrush* generated(Shape shape, int size, int feather);

	static bool initBrushes();

private:
	// For a brush made up on the spot rather than loaded from an icon
	SBrush(Shape shape, int size, int feather);

	unique_ptr<SImage> image_ = nullptr; // The cursor graphic
	string             name_;
	string             icon_;
	Vec2i              center_;
	bool               tiles_ = false; // True for the dither patterns
};
} // namespace slade
