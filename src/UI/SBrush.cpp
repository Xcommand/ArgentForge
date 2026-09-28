
// -----------------------------------------------------------------------------
// SLADE - It's a Doom Editor
// Copyright(C) 2008 - 2026 Simon Judd
//
// Email:       sirjuddington@gmail.com
// Web:         http://slade.mancubus.net
// Filename:    SBrush.cpp
// Description: SBrush class. Handles pixel painting for GfxCanvas.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 2 of the License, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along with
// this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA  02110 - 1301, USA.
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
//
// Includes
//
// -----------------------------------------------------------------------------
#include "Main.h"
#include "SBrush.h"
#include "App.h"
#include "Archive/ArchiveManager.h"

using namespace slade;


// -----------------------------------------------------------------------------
//
// Variables
//
// -----------------------------------------------------------------------------
namespace
{
vector<unique_ptr<SBrush>> brushes;

// The brushes that have been made from the shape and size settings, along with
// what those were for each. None of them can be thrown away when the settings move
// on, since a canvas that's using one just keeps pointing at it
struct GeneratedBrush
{
	SBrush::Shape    shape;
	int              size;
	int              feather;
	unique_ptr<SBrush> brush;
};
vector<GeneratedBrush> generated_brushes;

// Which letter the shape's icon goes by, so a made-up brush can show one of the
// old pictures on the toolbar
string shapeLetter(SBrush::Shape shape)
{
	switch (shape)
	{
	case SBrush::Shape::Circle: return "ci";
	case SBrush::Shape::Diamond: return "di";
	default: return "sq";
	}
}

// Which of those pictures a made-up brush shows: the toolbar only has the sizes
// the old fixed brushes came in, so it's the closest one going. Circles and
// diamonds were never drawn smaller than they were here, either
int shapeIconSize(SBrush::Shape shape, int size)
{
	int icon_size = std::clamp(size | 1, 1, 9);
	if (shape == SBrush::Shape::Circle)
		icon_size = std::max(icon_size, 5);
	else if (shape == SBrush::Shape::Diamond)
		icon_size = std::max(icon_size, 3);

	return icon_size;
}
} // namespace


// -----------------------------------------------------------------------------
//
// SBrush Class Functions
//
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// SBrush class constructor
// -----------------------------------------------------------------------------
SBrush::SBrush(const string& name) :
	name_{ name }, icon_{ strutil::afterFirst(name, '_') }, tiles_{ strutil::startsWith(name_, "pgfx_brush_pa_") }
{
	const auto res = app::archiveManager().programResourceArchive();
	if (res == nullptr)
		return;
	auto file = res->entryAtPath(fmt::format("icons/general/16/{}.png", icon_));
	if (file == nullptr || file->size() == 0)
	{
		log::error(2, "error, no file at icons/general/16/{}.png", icon_);
		return;
	}
	image_ = std::make_unique<SImage>();
	if (!image_->open(file->data(), 0, "png"))
	{
		log::error(2, "couldn't load image data for icons/general/16/{}.png", icon_);
		return;
	}
	image_->convertAlphaMap(SImage::AlphaSource::Alpha);

	// The pictures have soft edges so that they look nice on a button, but the
	// brushes they've always made are all-or-nothing ones. Only a brush built from
	// the shape settings below has an edge that fades away, so anything part way
	// in here counts as much as a fully covered pixel
	for (int y = 0; y < image_->height(); ++y)
		for (int x = 0; x < image_->width(); ++x)
		{
			uint8_t covered = image_->pixelIndexAt(x, y) > 127 ? 255 : 0;
			image_->setPixel(x, y, ColRGBA{ covered, covered, covered, covered });
		}

	center_.x = image_->width() >> 1;
	center_.y = image_->height() >> 1;
}

// -----------------------------------------------------------------------------
// Builds a brush of [shape] [size] pixels across, its edge fading away over the
// last [feather] of them. The image is an alpha map, so every pixel carries how
// strongly the brush affects it instead of just whether it does at all
// -----------------------------------------------------------------------------
SBrush::SBrush(Shape shape, int size, int feather) :
	name_{ fmt::format("brush_{}_{}x{}", shapeLetter(shape), size, feather) },
	icon_{ fmt::format("brush_{}_{}", shapeLetter(shape), shapeIconSize(shape, size)) }
{
	// The shape is measured from the middle of the pixels, which for an even size
	// falls between two of them, while [half] is how far the shape reaches from
	// there. [inside] is then how deep a pixel sits within the edge
	const double middle = (size - 1) / 2.0;
	const double half   = size / 2.0;

	image_ = std::make_unique<SImage>();
	image_->create(size, size, SImage::Type::AlphaMap);

	for (int y = 0; y < size; ++y)
	{
		const double dy = std::abs(y - middle);
		for (int x = 0; x < size; ++x)
		{
			const double dx = std::abs(x - middle);

			double inside = half;
			if (shape == Shape::Square)
				inside -= std::max(dx, dy);
			else if (shape == Shape::Circle)
				inside -= std::sqrt(dx * dx + dy * dy);
			else
				inside -= dx + dy;

			// Without a feather a pixel is either in the shape or out of it. With
			// one, it's as strong as far in from the edge as it has left of feather
			double strength = 0.0;
			if (feather > 0)
				strength = std::clamp(inside / (double)feather, 0.0, 1.0);
			else if (inside >= 0.0)
				strength = 1.0;

			const uint8_t shade = (uint8_t)(strength * 255.0);
			image_->setPixel(x, y, ColRGBA{ shade, shade, shade, shade });
		}
	}

	center_.x = size >> 1;
	center_.y = size >> 1;
}

// -----------------------------------------------------------------------------
// Returns intensity of how much this pixel is affected by the brush;
// [0, 0] is the brush's center
// -----------------------------------------------------------------------------
uint8_t SBrush::pixel(int x, int y) const
{
	x += center_.x;
	y += center_.y;
	if (image_ && x >= 0 && x < image_->width() && y >= 0 && y < image_->height())
		return image_->pixelIndexAt(static_cast<unsigned>(x), static_cast<unsigned>(y));
	return 0;
}


// -----------------------------------------------------------------------------
//
// SBrush Class Static Functions
//
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// Get a brush from its name
// -----------------------------------------------------------------------------
SBrush* SBrush::get(const string& name)
{
	for (auto& brush : brushes)
		if (strutil::equalCI(name, brush->name()))
			return brush.get();

	return nullptr;
}

// -----------------------------------------------------------------------------
// The name a shape goes by in the settings, and the shape saved under [name]
// -----------------------------------------------------------------------------
const char* SBrush::shapeName(Shape shape)
{
	switch (shape)
	{
	case Shape::Circle: return "circle";
	case Shape::Diamond: return "diamond";
	default: return "square";
	}
}

SBrush::Shape SBrush::shapeFromName(const string& name)
{
	if (strutil::equalCI(name, "circle"))
		return Shape::Circle;
	if (strutil::equalCI(name, "diamond"))
		return Shape::Diamond;
	return Shape::Square;
}

// -----------------------------------------------------------------------------
// Returns the brush for the given shape and size, making one if that combination
// hasn't been asked for before
// -----------------------------------------------------------------------------
SBrush* SBrush::generated(Shape shape, int size, int feather)
{
	size    = std::max(1, size);
	feather = std::max(0, feather);

	for (auto& entry : generated_brushes)
		if (entry.shape == shape && entry.size == size && entry.feather == feather)
			return entry.brush.get();

	generated_brushes.push_back({ shape, size, feather, unique_ptr<SBrush>(new SBrush(shape, size, feather)) });
	return generated_brushes.back().brush.get();
}

// -----------------------------------------------------------------------------
// Init brushes
// -----------------------------------------------------------------------------
bool SBrush::initBrushes()
{
	brushes.emplace_back(new SBrush("pgfx_brush_sq_1"));
	brushes.emplace_back(new SBrush("pgfx_brush_sq_3"));
	brushes.emplace_back(new SBrush("pgfx_brush_sq_5"));
	brushes.emplace_back(new SBrush("pgfx_brush_sq_7"));
	brushes.emplace_back(new SBrush("pgfx_brush_sq_9"));
	brushes.emplace_back(new SBrush("pgfx_brush_ci_5"));
	brushes.emplace_back(new SBrush("pgfx_brush_ci_7"));
	brushes.emplace_back(new SBrush("pgfx_brush_ci_9"));
	brushes.emplace_back(new SBrush("pgfx_brush_di_3"));
	brushes.emplace_back(new SBrush("pgfx_brush_di_5"));
	brushes.emplace_back(new SBrush("pgfx_brush_di_7"));
	brushes.emplace_back(new SBrush("pgfx_brush_di_9"));
	brushes.emplace_back(new SBrush("pgfx_brush_pa_a"));
	brushes.emplace_back(new SBrush("pgfx_brush_pa_b"));
	brushes.emplace_back(new SBrush("pgfx_brush_pa_c"));
	brushes.emplace_back(new SBrush("pgfx_brush_pa_d"));
	brushes.emplace_back(new SBrush("pgfx_brush_pa_e"));
	brushes.emplace_back(new SBrush("pgfx_brush_pa_f"));
	brushes.emplace_back(new SBrush("pgfx_brush_pa_g"));
	brushes.emplace_back(new SBrush("pgfx_brush_pa_h"));
	brushes.emplace_back(new SBrush("pgfx_brush_pa_i"));
	brushes.emplace_back(new SBrush("pgfx_brush_pa_j"));
	brushes.emplace_back(new SBrush("pgfx_brush_pa_k"));
	brushes.emplace_back(new SBrush("pgfx_brush_pa_l"));
	brushes.emplace_back(new SBrush("pgfx_brush_pa_m"));
	brushes.emplace_back(new SBrush("pgfx_brush_pa_n"));
	brushes.emplace_back(new SBrush("pgfx_brush_pa_o"));
	return true;
}
