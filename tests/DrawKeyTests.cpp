// Shapes and text say what their drawing depends on (RendererComponent::
// GetDrawKey): each input they draw from changes the key, and frames redrawn
// with the renderer reusing what it measured of them match frames drawn whole.

#include <gtest/gtest.h>

#include <BitmapFont.h>
#include <Sprite.h>
#include <ShapeComponent.h>
#include <TextComponent.h>
#include <PixelFormat.h>
#include <deki/Object.h>
#include <deki/Scene.h>
#include <deki/assets/Texture2D.h>
#include <deki/providers/Buffer.h>
#include "deki-rendering/CameraComponent.h"
#include "deki-rendering/DirtyRegion.h"
#include "deki-rendering/Standard2DRenderer.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

using namespace Deki2D;
using namespace DekiRendering;

namespace
{
constexpr int kW = 64, kH = 48;

const Deki::ComponentTypeInfo kShapeInfo =
    Deki::MakeHandWrittenTypeInfo<ShapeComponent, RendererComponent>(/*declaresUpdate=*/false);
const Deki::ComponentTypeInfo kTextInfo =
    Deki::MakeHandWrittenTypeInfo<TextComponent, RendererComponent>(/*declaresUpdate=*/false);

// Three glyphs, 'A' to 'C', 3 x 5 each, side by side in an alpha atlas, each
// with its own pattern so a changed letter changes the pixels.
std::unique_ptr<BitmapFont> TinyFont()
{
    constexpr int kAtlasW = 9, kAtlasH = 5;
    std::vector<uint8_t> alpha(kAtlasW * kAtlasH);
    for (int y = 0; y < kAtlasH; ++y)
    {
        for (int x = 0; x < kAtlasW; ++x)
        {
            const int glyph = x / 3;
            alpha[y * kAtlasW + x] = ((x + y * (glyph + 1)) % 3 == 0) ? 0 : ((x * y) % 2 ? 140 : 255);
        }
    }
    Deki::Texture2D::Header header{};
    std::memcpy(header.magic, "2DTX", 4);
    header.version = 1;
    header.width = kAtlasW;
    header.height = kAtlasH;
    header.format = Deki::Texture2D::TextureFormat::ALPHA8;
    header.dataSize = static_cast<uint32_t>(alpha.size());
    std::vector<uint8_t> file(sizeof(header) + alpha.size());
    std::memcpy(file.data(), &header, sizeof(header));
    std::memcpy(file.data() + sizeof(header), alpha.data(), alpha.size());
    Deki::Texture2D* atlas = Sprite::LoadFromFileData(file.data(), file.size());
    if (!atlas)
    {
        return nullptr;
    }
    Deki::Buffer<GlyphInfo> glyphs;
    if (!glyphs.Allocate(3, Deki::Memory::Internal))
    {
        delete atlas;
        return nullptr;
    }
    for (int i = 0; i < 3; ++i)
    {
        glyphs[i] = GlyphInfo{ static_cast<uint16_t>(i * 3), 0, 3, 5, 0, -5, 4 };
    }
    return std::unique_ptr<BitmapFont>(BitmapFont::CreateFromMemory(atlas, std::move(glyphs), 'A', 'C', 6, 5));
}

struct Hud
{
    Deki::Scene scene;
    CameraComponent* camera = nullptr;
    ShapeComponent* side = nullptr;
    ShapeComponent* face = nullptr;
    ShapeComponent* edge = nullptr;
    TextComponent* label = nullptr;
    Standard2DRenderer renderer;

    explicit Hud(BitmapFont* font)
    {
        auto* camObj = new Deki::Object("Camera");
        camera = camObj->AddComponent<CameraComponent>();
        camera->orthoHeight = 1.5f;  // 16 px/m on 64 x 48
        camera->clearColor = Deki::Color(20, 90, 40);
        scene.AddObject(camObj);
        // A raised panel of three shapes, a label on it, as a HUD is built.
        side = AddShape("Side", 0.0f, -0.0625f, 2.5f, 1.25f, Deki::Color(226, 198, 156), 1);
        face = AddShape("Face", 0.0f, 0.0f, 2.5f, 1.125f, Deki::Color(255, 239, 208), 2);
        edge = AddShape("Edge", 0.0f, -0.0625f, 2.5f, 1.25f, Deki::Color(107, 64, 36), 3);
        edge->lineWidth = 0.0625f;
        auto* t = new Deki::Object("Label");
        t->SetX(-0.5f);
        scene.AddObject(t);
        label = t->AddComponent<TextComponent>();
        label->SetTypeInfo(&kTextInfo);
        label->font = font;
        label->text = "ABC";
        label->width = 2.0f;
        label->height = 0.75f;
        label->color = Deki::Color(107, 64, 36);
        label->sortingOrder = 4;
    }

    ShapeComponent* AddShape(const char* name, float x, float y, float w, float h, Deki::Color c, int order)
    {
        auto* o = new Deki::Object(name);
        o->SetX(x);
        o->SetY(y);
        scene.AddObject(o);
        auto* s = o->AddComponent<ShapeComponent>();
        s->SetTypeInfo(&kShapeInfo);
        s->kind = ShapeKind::Rectangle;
        s->width = w;
        s->height = h;
        s->cornerRadius = 0.375f;
        s->color = c;
        s->sortingOrder = order;
        return s;
    }

    void Clear(std::vector<uint8_t>& frame, const Deki::Rect& r) const
    {
        const uint16_t c = DekiPixel::PackRGB565(camera->clearColor.r, camera->clearColor.g, camera->clearColor.b);
        for (int y = r.top; y < r.bottom; ++y)
        {
            for (int x = r.left; x < r.right; ++x)
            {
                DekiPixel::Store565(&frame[(static_cast<size_t>(y) * kW + x) * 2], c);
            }
        }
    }

    std::vector<uint8_t> Whole()
    {
        std::vector<uint8_t> frame(static_cast<size_t>(kW) * kH * 2);
        Clear(frame, Deki::Rect{ 0, 0, kW, kH });
        RenderContext ctx{ camera, frame.data(), kW, kH, Deki::ColorFormat::RGB565 };
        renderer.Render(&scene, ctx);
        return frame;
    }

    void Changes(std::vector<uint8_t>& frame, uint32_t id)
    {
        RenderContext ctx{ camera, frame.data(), kW, kH, Deki::ColorFormat::RGB565 };
        ctx.contentFrame = id;
        DirtyRegion changed;
        ASSERT_TRUE(renderer.FindChanges(&scene, ctx, changed));
        if (changed.IsFull())
        {
            frame = Whole();
            return;
        }
        for (const Deki::Rect& part : changed.Rects())
        {
            Clear(frame, part);
            RenderContext p = ctx;
            p.redraw = part;
            renderer.Render(&scene, p);
        }
    }
};

uint64_t KeyOf(const RendererComponent& r)
{
    uint64_t key = 0;
    EXPECT_TRUE(r.GetDrawKey(key));
    return key;
}
}  // namespace

TEST(DrawKeys, EveryInputOfAShapeChangesItsKey)
{
    ShapeComponent s;
    s.width = 2.0f;
    s.height = 1.0f;
    s.color = Deki::Color(10, 20, 30);
    const uint64_t base = KeyOf(s);
    EXPECT_EQ(KeyOf(s), base) << "the same shape, the same key";
    auto changes = [&](const char* what, auto change, auto undo)
    {
        change();
        EXPECT_NE(KeyOf(s), base) << what;
        undo();
        EXPECT_EQ(KeyOf(s), base) << what << ", undone";
    };
    changes("width", [&] { s.width = 2.5f; }, [&] { s.width = 2.0f; });
    changes("height", [&] { s.height = 1.5f; }, [&] { s.height = 1.0f; });
    changes("corner radius", [&] { s.cornerRadius = 0.25f; }, [&] { s.cornerRadius = 0.0f; });
    changes("line width", [&] { s.lineWidth = 0.0625f; }, [&] { s.lineWidth = 0.0f; });
    changes("colour", [&] { s.color = Deki::Color(10, 20, 31); }, [&] { s.color = Deki::Color(10, 20, 30); });
    changes("alpha", [&] { s.color.a = 128; }, [&] { s.color.a = 255; });
    changes("kind", [&] { s.kind = ShapeKind::Ellipse; }, [&] { s.kind = ShapeKind::Rectangle; });
    changes("start angle", [&] { s.startAngle = 0.5f; }, [&] { s.startAngle = 0.0f; });
    changes("end angle", [&] { s.endAngle = 3.0f; }, [&] { s.endAngle = 6.2831853f; });
    changes("round caps", [&] { s.roundCaps = true; }, [&] { s.roundCaps = false; });
}

TEST(DrawKeys, EveryInputOfTextChangesItsKey)
{
    auto font = TinyFont();
    ASSERT_TRUE(font);
    TextComponent t;
    uint64_t key = 0;
    EXPECT_FALSE(t.GetDrawKey(key)) << "no font, no text: measured in full";
    t.font = font.get();
    EXPECT_FALSE(t.GetDrawKey(key)) << "no text";
    t.text = "AB";
    const float width0 = t.width, height0 = t.height;
    const Deki::Color color0 = t.color, decoration0 = t.decorationColor;
    const TextAlign align0 = t.align;
    const TextVerticalAlign vertical0 = t.verticalAlign;
    const int32_t scale0 = t.pixelScale;
    const uint64_t base = KeyOf(t);
    auto changes = [&](const char* what, auto change, auto undo)
    {
        change();
        EXPECT_NE(KeyOf(t), base) << what;
        undo();
        EXPECT_EQ(KeyOf(t), base) << what << ", undone";
    };
    changes("text", [&] { t.text = "AC"; }, [&] { t.text = "AB"; });
    changes("longer text", [&] { t.text = "ABA"; }, [&] { t.text = "AB"; });
    changes("width", [&] { t.width = width0 + 1.0f; }, [&] { t.width = width0; });
    changes("height", [&] { t.height = height0 + 0.5f; }, [&] { t.height = height0; });
    changes("colour", [&] { t.color = Deki::Color(1, 2, 3); }, [&] { t.color = color0; });
    changes("decoration colour", [&] { t.decorationColor = Deki::Color(1, 2, 3); },
            [&] { t.decorationColor = decoration0; });
    changes("align", [&] { t.align = TextAlign::Right; }, [&] { t.align = align0; });
    changes("vertical align", [&] { t.verticalAlign = TextVerticalAlign::Top; },
            [&] { t.verticalAlign = vertical0; });
    changes("pixel scale", [&] { t.pixelScale = scale0 + 1; }, [&] { t.pixelScale = scale0; });
}

// A HUD of shapes and a label, redrawn only where it changed while the
// renderer reuses what it measured of the rest: every frame is the frame
// drawn whole.
TEST(DrawKeys, AHudRedrawnWithReuseMatchesOneDrawnWhole)
{
    auto font = TinyFont();
    ASSERT_TRUE(font);
    Hud hud(font.get());
    Hud reference(font.get());
    std::vector<uint8_t> frame = hud.Whole();
    uint32_t id = 1;
    hud.Changes(frame, id++);  // the first: everything
    hud.Changes(frame, id++);  // nothing changed: reused
    hud.Changes(frame, id++);
    EXPECT_EQ(frame, reference.Whole());

    // The label counts up, as a score does.
    for (const char* score : { "B", "BA", "BB", "CAB" })
    {
        hud.label->text = score;
        reference.label->text = score;
        hud.Changes(frame, id++);
        EXPECT_EQ(frame, reference.Whole()) << "score " << score;
        hud.Changes(frame, id++);
        EXPECT_EQ(frame, reference.Whole()) << "score " << score << ", one frame on";
    }
    // A layer of the panel changes colour, then size, then back.
    hud.face->color = Deki::Color(250, 200, 120);
    reference.face->color = Deki::Color(250, 200, 120);
    hud.Changes(frame, id++);
    EXPECT_EQ(frame, reference.Whole()) << "the face's colour";
    hud.side->height = 1.5f;
    reference.side->height = 1.5f;
    hud.Changes(frame, id++);
    EXPECT_EQ(frame, reference.Whole()) << "the side taller";
    hud.side->height = 1.25f;
    reference.side->height = 1.25f;
    hud.label->color = Deki::Color(200, 30, 30);
    reference.label->color = Deki::Color(200, 30, 30);
    hud.Changes(frame, id++);
    EXPECT_EQ(frame, reference.Whole()) << "back, and the label red";
    hud.Changes(frame, id++);
    EXPECT_EQ(frame, reference.Whole()) << "one frame on";
}
