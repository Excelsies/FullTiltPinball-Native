#include "pch.h"
#include "gdrv.h"

#include "GroupData.h"
#include "partman.h"
#include "pb.h"
#include "score.h"
#include "winmain.h"
#include "TTextBox.h"
#include "fullscrn.h"
#include <atomic>

ColorRgba gdrv::current_palette[256]{};

gdrv_bitmap8::gdrv_bitmap8(int width, int height) : gdrv_bitmap8(width, height, true, true)
{
}

gdrv_bitmap8::gdrv_bitmap8(int width, int height, bool indexed) : gdrv_bitmap8(width, height, indexed, true)
{
}

gdrv_bitmap8::gdrv_bitmap8(int width, int height, bool indexed, bool bmpBuff)
{
	assertm(width >= 0 && height >= 0, "Negative bitmap8 dimensions");

	Width = width;
	Height = height;
	Stride = width;
	IndexedStride = width;
	BitmapType = BitmapTypes::DibBitmap;
	Texture = nullptr;
	IndexedBmpPtr = nullptr;
	BmpBufPtr1 = nullptr;
	XPosition = 0;
	YPosition = 0;
	Resolution = 0;

	if (indexed)
		IndexedBmpPtr = new char[Height * IndexedStride];
	if (bmpBuff)
		BmpBufPtr1 = new ColorRgba[Height * Stride];
}

gdrv_bitmap8::gdrv_bitmap8(const dat8BitBmpHeader& header)
{
	assertm(header.Width >= 0 && header.Height >= 0, "Negative bitmap8 dimensions");

	if (header.IsFlagSet(bmp8Flags::Spliced))
		BitmapType = BitmapTypes::Spliced;
	else if (header.IsFlagSet(bmp8Flags::DibBitmap))
		BitmapType = BitmapTypes::DibBitmap;
	else
		BitmapType = BitmapTypes::RawBitmap;

	Width = header.Width;
	Stride = header.Width;
	IndexedStride = header.Width;
	Height = header.Height;
	XPosition = header.XPosition;
	YPosition = header.YPosition;
	Resolution = header.Resolution;
	Texture = nullptr;

	int sizeInBytes;
	if (BitmapType == BitmapTypes::Spliced)
	{
		sizeInBytes = header.Size;
	}
	else
	{
		if (BitmapType == BitmapTypes::RawBitmap)
			assertm(Width % 4 == 0 || header.IsFlagSet(bmp8Flags::RawBmpUnaligned), "Wrong raw bitmap align flag");
		if (Width % 4)
			IndexedStride = Width - Width % 4 + 4;
		sizeInBytes = Height * IndexedStride;
		assertm(sizeInBytes == header.Size, "Wrong bitmap8 size");
	}

	IndexedBmpPtr = new char[sizeInBytes];
	BmpBufPtr1 = new ColorRgba[Stride * Height];
}

gdrv_bitmap8::~gdrv_bitmap8()
{
	if (BitmapType != BitmapTypes::None)
	{
		delete[] BmpBufPtr1;
		delete[] IndexedBmpPtr;
		if (Texture)
			SDL_DestroyTexture(Texture);
	}
}

void gdrv_bitmap8::ScaleIndexed(float scaleX, float scaleY)
{
	if (!IndexedBmpPtr)
	{
		assertm(false, "Scaling non-indexed bitmap");
		return;
	}

	int newWidht = static_cast<int>(Width * scaleX), newHeight = static_cast<int>(Height * scaleY);
	if (Width == newWidht && Height == newHeight)
		return;

	auto newIndBuf = new char[newHeight * newWidht];
	for (int dst = 0, y = 0; y < newHeight; y++)
	{
		for (int x = 0; x < newWidht; x++, dst++)
		{
			auto px = static_cast<int>(x / scaleX);
			auto py = static_cast<int>(y / scaleY);
			newIndBuf[dst] = IndexedBmpPtr[(py * IndexedStride) + px];
		}
	}

	Stride = IndexedStride = Width = newWidht;
	Height = newHeight;

	delete IndexedBmpPtr;
	IndexedBmpPtr = newIndBuf;
	delete BmpBufPtr1;
	BmpBufPtr1 = new ColorRgba[Stride * Height];
}

// SDL_RENDER_TARGETS_RESET / SDL_RENDER_DEVICE_RESET: the renderer lost its textures, the next upload must be complete.
static std::atomic<unsigned> RenderResetCount{ 0 };

static int RenderResetWatch(void*, SDL_Event* event)
{
	if (event->type == SDL_RENDER_TARGETS_RESET || event->type == SDL_RENDER_DEVICE_RESET)
		RenderResetCount++;
	return 0;
}

void gdrv_bitmap8::CreateTexture(const char* scaleHint, int access)
{
	if (Texture != nullptr)
	{
		SDL_DestroyTexture(Texture);
	}

	UsingSdlHint hint{ SDL_HINT_RENDER_SCALE_QUALITY, scaleHint };
	Texture = SDL_CreateTexture
	(
		winmain::Renderer,
		SDL_PIXELFORMAT_BGRA32,
		access,
		Width, Height
	);
	SDL_SetTextureBlendMode(Texture, SDL_BLENDMODE_NONE);

	static bool watchAdded = false;
	if (!watchAdded)
	{
		SDL_AddEventWatch(RenderResetWatch, nullptr);
		watchAdded = true;
	}
	SeenRenderResets = RenderResetCount;
	MarkAllDirty();
}

void gdrv_bitmap8::MarkAllDirty()
{
	AllDirty = true;
	DirtyRects.clear();
}

void gdrv_bitmap8::MarkDirty(int x, int y, int width, int height)
{
	if (!Texture || AllDirty)
		return;

	// Clamp to the bitmap
	int x1 = std::min(x + width, Width), y1 = std::min(y + height, Height);
	x = std::max(x, 0);
	y = std::max(y, 0);
	if (x >= x1 || y >= y1)
		return;

	// Merge with every listed rectangle it overlaps or touches
	for (size_t i = 0; i < DirtyRects.size();)
	{
		const auto& e = DirtyRects[i];
		if (e.x <= x1 && x <= e.x + e.w && e.y <= y1 && y <= e.y + e.h)
		{
			x = std::min(x, e.x);
			y = std::min(y, e.y);
			x1 = std::max(x1, e.x + e.w);
			y1 = std::max(y1, e.y + e.h);
			DirtyRects.erase(DirtyRects.begin() + i);
			i = 0; // the grown rectangle may now reach others
		}
		else
			++i;
	}
	DirtyRects.push_back(SDL_Rect{ x, y, x1 - x, y1 - y });

	if (DirtyRects.size() > MaxDirtyRects)
	{
		// Too many pieces: keep one bounding rectangle
		int bx0 = Width, by0 = Height, bx1 = 0, by1 = 0;
		for (const auto& e : DirtyRects)
		{
			bx0 = std::min(bx0, e.x);
			by0 = std::min(by0, e.y);
			bx1 = std::max(bx1, e.x + e.w);
			by1 = std::max(by1, e.y + e.h);
		}
		DirtyRects.assign(1, SDL_Rect{ bx0, by0, bx1 - bx0, by1 - by0 });
	}
}

// SDL_UpdateTexture with a rectangle: on the Pi's GLES2 driver SDL_LockTexture / SDL_UnlockTexture costs about 14 ms
// per call whatever the rectangle (measured, tools/upbench.c), SDL_UpdateTexture of a small rectangle under 1 ms.
static void UploadRect(SDL_Texture* texture, const ColorRgba* src, int stride, const SDL_Rect& rect)
{
	auto result = SDL_UpdateTexture(texture, &rect, src + static_cast<size_t>(rect.y) * stride + rect.x,
	                                stride * static_cast<int>(sizeof(ColorRgba)));
	assertm(result == 0, "Updating vScreen texture failed");
}

#ifdef FT_VSCREEN_VERIFY
// Test build: keeps a copy of what the texture holds and reports every pixel that changed without being declared.
static void VerifyTracking(const gdrv_bitmap8& bmp, bool all)
{
	static std::vector<ColorRgba> shadow;
	static SDL_Texture* shadowTexture = nullptr;
	static unsigned long long frames = 0, misses = 0, bytes = 0;
	const size_t count = static_cast<size_t>(bmp.Width) * bmp.Height;
	frames++;
	if (all || shadowTexture != bmp.Texture || shadow.size() != count)
	{
		shadow.assign(bmp.BmpBufPtr1, bmp.BmpBufPtr1 + count);
		shadowTexture = bmp.Texture;
		bytes += count * sizeof(ColorRgba);
	}
	else
	{
		int shown = 0;
		for (int y = 0; y < bmp.Height; y++)
		{
			for (int x = 0; x < bmp.Width; x++)
			{
				auto idx = static_cast<size_t>(y) * bmp.Width + x;
				if (bmp.BmpBufPtr1[idx].Color == shadow[idx].Color)
					continue;
				bool declared = false;
				for (const auto& r : bmp.DirtyRects)
					declared |= x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
				if (!declared)
				{
					misses++;
					if (shown++ < 3)
						std::fprintf(stderr, "vscreen-verify: MISS undeclared change at %d,%d (frame %llu)\n", x, y, frames);
				}
			}
		}
		for (const auto& r : bmp.DirtyRects)
		{
			bytes += static_cast<size_t>(r.w) * r.h * sizeof(ColorRgba);
			for (int y = r.y; y < r.y + r.h; y++)
				std::memcpy(&shadow[static_cast<size_t>(y) * bmp.Width + r.x],
				            &bmp.BmpBufPtr1[static_cast<size_t>(y) * bmp.Width + r.x], r.w * sizeof(ColorRgba));
		}
	}
	if (frames % 500 == 0)
		std::fprintf(stderr, "vscreen-verify: frames %llu, misses %llu, bytes uploaded %llu (full would be %llu)\n",
		             frames, misses, bytes, frames * count * sizeof(ColorRgba));
}
#endif

void gdrv_bitmap8::BlitToTexture()
{
	assertm(Texture, "Updating null texture");
	assertm(Stride == Width, "Padding on vScreen bitmap");

	// A reset renderer lost the texture contents
	auto resets = RenderResetCount.load();
	if (resets != SeenRenderResets)
	{
		SeenRenderResets = resets;
		MarkAllDirty();
	}

	LastUploadBytes = 0;
	if (!AllDirty && DirtyRects.empty())
		return;

#ifdef FT_VSCREEN_VERIFY
	VerifyTracking(*this, AllDirty);
#endif

	if (AllDirty)
	{
		UploadRect(Texture, BmpBufPtr1, Stride, SDL_Rect{ 0, 0, Width, Height });
		LastUploadBytes = static_cast<size_t>(Width) * Height * sizeof(ColorRgba);
	}
	else
	{
		for (const auto& rect : DirtyRects)
		{
			UploadRect(Texture, BmpBufPtr1, Stride, rect);
			LastUploadBytes += static_cast<size_t>(rect.w) * rect.h * sizeof(ColorRgba);
		}
	}
	AllDirty = false;
	DirtyRects.clear();
}

int gdrv::display_palette(ColorRgba* plt)
{
	// Colors from Windows system palette
	const ColorRgba sysPaletteColors[10]
	{
		ColorRgba{0, 0, 0, 0}, // Color 0: transparent
		ColorRgba{0x80, 0, 0, 0xff},
		ColorRgba{0, 0x80, 0, 0xff},
		ColorRgba{0x80, 0x80, 0, 0xff},
		ColorRgba{0, 0, 0x80, 0xff},
		ColorRgba{0x80, 0, 0x80, 0xff},
		ColorRgba{0, 0x80, 0x80, 0xff},
		ColorRgba{0xC0, 0xC0, 0xC0, 0xff},
		ColorRgba{0xC0, 0xDC, 0xC0, 0xff},
		ColorRgba{0xA6, 0xCA, 0xF0, 0xff},
	};

	std::memset(current_palette, 0, sizeof current_palette);
	std::memcpy(current_palette, sysPaletteColors, sizeof sysPaletteColors);	

	for (int index = 10; plt && index < 246; index++)
	{
		auto srcClr = plt[index];
		srcClr.SetAlpha(0xff);		
		current_palette[index] = ColorRgba{ srcClr };
		current_palette[index].SetAlpha(2);
	}

	current_palette[255] = ColorRgba::White();

	for (const auto group : pb::record_table->Groups)
	{
		for (int i = 0; i <= 2; i++)
		{
			for (int k = 0; k < GroupData::MaxBitmapsPerResolution; k++)
			{
				auto bmp = group->GetBitmap(i, k);
				if (bmp)
				{
					ApplyPalette(*bmp);
				}
			}
		}
	}

	return 0;
}

void gdrv::fill_bitmap(gdrv_bitmap8* bmp, int width, int height, int xOff, int yOff, uint8_t fillChar)
{
	fill_bitmap(bmp, width, height, xOff, yOff, current_palette[fillChar]);
}

void gdrv::fill_bitmap(gdrv_bitmap8* bmp, int width, int height, int xOff, int yOff, ColorRgba fillColor)
{
	bmp->MarkDirty(xOff, yOff, width, height);
	auto bmpPtr = &bmp->BmpBufPtr1[bmp->Width * yOff + xOff];
	for (; height > 0; --height)
	{
		for (int x = width; x > 0; --x)
			*bmpPtr++ = fillColor;
		bmpPtr += bmp->Stride - width;
	}
}

void gdrv::copy_bitmap(gdrv_bitmap8* dstBmp, int width, int height, int xOff, int yOff, gdrv_bitmap8* srcBmp,
                       int srcXOff, int srcYOff)
{
	dstBmp->MarkDirty(xOff, yOff, width, height);
	auto srcPtr = &srcBmp->BmpBufPtr1[srcBmp->Stride * srcYOff + srcXOff];
	auto dstPtr = &dstBmp->BmpBufPtr1[dstBmp->Stride * yOff + xOff];

	for (int y = height; y > 0; --y)
	{
		std::memcpy(dstPtr, srcPtr, width * sizeof(ColorRgba));
		srcPtr += srcBmp->Stride;
		dstPtr += dstBmp->Stride;
	}
}

void gdrv::copy_bitmap_w_transparency(gdrv_bitmap8* dstBmp, int width, int height, int xOff, int yOff,
                                      gdrv_bitmap8* srcBmp, int srcXOff, int srcYOff)
{
	dstBmp->MarkDirty(xOff, yOff, width, height);
	auto srcPtr = &srcBmp->BmpBufPtr1[srcBmp->Stride * srcYOff + srcXOff];
	auto dstPtr = &dstBmp->BmpBufPtr1[dstBmp->Stride * yOff + xOff];

	for (int y = height; y > 0; --y)
	{
		for (int x = width; x > 0; --x)
		{
			if ((*srcPtr).Color)
				*dstPtr = *srcPtr;
			++srcPtr;
			++dstPtr;
		}

		srcPtr += srcBmp->Stride - width;
		dstPtr += dstBmp->Stride - width;
	}
}

void gdrv::ScrollBitmapHorizontal(gdrv_bitmap8* bmp, int xStart)
{
	bmp->MarkAllDirty();
	auto srcPtr = bmp->BmpBufPtr1;
	auto startOffset = xStart >= 0 ? 0 : -xStart;
	auto endOffset = xStart >= 0 ? xStart : 0;
	auto length = bmp->Width - std::abs(xStart);
	for (int y = bmp->Height; y > 0; --y)
	{
		std::memmove(srcPtr + endOffset, srcPtr + startOffset, length * sizeof(ColorRgba));
		srcPtr += bmp->Stride;
	}
}


void gdrv::grtext_draw_ttext_in_box()
{
	for (const auto textBox : { pb::InfoTextBox, pb::MissTextBox })
	{
		if (textBox)
		{
			textBox->DrawImGui();
		}
	}
}

void gdrv::ApplyPalette(gdrv_bitmap8& bmp)
{
	if (bmp.BitmapType == BitmapTypes::None)
		return;
	assertm(bmp.BitmapType != BitmapTypes::Spliced, "gdrv: wrong bitmap type");
	assertm(bmp.IndexedBmpPtr != nullptr, "gdrv: non-indexed bitmap");

	// Apply palette, flip horizontally 
	auto dst = bmp.BmpBufPtr1;
	for (auto y = bmp.Height - 1; y >= 0; y--)
	{
		auto src = reinterpret_cast<uint8_t*>(bmp.IndexedBmpPtr) + bmp.IndexedStride * y;
		for (auto x = 0; x < bmp.Width; x++)
		{
			*dst++ = current_palette[*src++];
		}
	}
}

void gdrv::CreatePreview(gdrv_bitmap8& bmp)
{
	if (bmp.Texture)
		return;

	bmp.CreateTexture("nearest", SDL_TEXTUREACCESS_STATIC);
	SDL_UpdateTexture(bmp.Texture, nullptr, bmp.BmpBufPtr1, bmp.Width * 4);
}
