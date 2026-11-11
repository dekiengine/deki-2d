// A SpriteBatchComponent draws the pixels its pieces would as sprite objects
// of their own, sorted among other objects the same way, in whole frames and
// in the parts of a frame redrawn after a piece changes.

#include <gtest/gtest.h>

#include <Sprite.h>
#include <SpriteBatchComponent.h>
#include <SpriteComponent.h>
#include <PixelFormat.h>
#include <deki/Object.h>
#include <deki/Scene.h>
#include <deki/assets/Texture2D.h>
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
using Format = Deki::Texture2D::TextureFormat;

// This test build has no generated registration: the renderers get theirs by
// hand, so the 2D renderer knows them for renderers.
const Deki::ComponentTypeInfo kSpriteInfo =
    Deki::MakeHandWrittenTypeInfo<SpriteComponent, RendererComponent>(/*declaresUpdate=*/false);
const Deki::ComponentTypeInfo kBatchInfo =
    Deki::MakeHandWrittenTypeInfo<SpriteBatchComponent, RendererComponent>(/*declaresUpdate=*/false);

// A 16 x 8 sheet of two 8 x 8 frames, with see-through, half see-through and
// opaque pixels in a pattern, so what draws over what shows.
std::unique_ptr<Sprite> Sheet()
{
    std::vector<uint8_t> pixels;
    for (int y = 0; y < 8; ++y)
    {
        for (int x = 0; x < 16; ++x)
        {
            const uint16_t c = DekiPixel::PackRGB565(static_cast<uint8_t>(x * 16), static_cast<uint8_t>(y * 32),
                                                     static_cast<uint8_t>(x < 8 ? 200 : 40));
            const uint8_t a = (x + y) % 5 == 0 ? 0 : ((x * y) % 3 == 0 ? 128 : 255);
            pixels.push_back(static_cast<uint8_t>(c & 0xFF));
            pixels.push_back(static_cast<uint8_t>(c >> 8));
            pixels.push_back(a);
        }
    }
    Deki::Texture2D::Header header{};
    std::memcpy(header.magic, "2DTX", 4);
    header.version = 1;
    header.width = 16;
    header.height = 8;
    header.format = Format::RGB565A8;
    header.dataSize = static_cast<uint32_t>(pixels.size());
    header.flags = DTEX_FLAG_HAS_ALPHA;
    std::vector<uint8_t> file(sizeof(header) + pixels.size());
    std::memcpy(file.data(), &header, sizeof(header));
    std::memcpy(file.data() + sizeof(header), pixels.data(), pixels.size());
    return std::unique_ptr<Sprite>(Sprite::LoadFromFileData(file.data(), file.size()));
}

struct PieceSpec
{
    float x, y;
    int frame;  // 0 or 1
    int order;
    bool flip;
};

const PieceSpec kPieces[] = {
    { -1.0f, 0.5f, 0, 10, false }, { -0.75f, 0.6f, 1, 30, true }, { -0.5f, 0.4f, 0, 20, false },
    { 0.25f, -0.2f, 1, 5, false }, { 0.5f, 0.0f, 0, 40, true },   { 1.0f, 0.3f, 1, 25, false },
};

struct World
{
    Deki::Scene scene;
    CameraComponent* camera = nullptr;
    Deki::Object* holder = nullptr;
    SpriteBatchComponent* batch = nullptr;
    std::vector<SpriteComponent*> sprites;
    Standard2DRenderer renderer;

    World(Sprite* sheet, Sprite* walker, bool batched)
    {
        auto* camObj = new Deki::Object("Camera");
        camera = camObj->AddComponent<CameraComponent>();
        camera->orthoHeight = 1.5f;
        camera->clearColor = Deki::Color(0, 0, 64);
        scene.AddObject(camObj);

        // The pieces hang off a moved, scaled object, as children would.
        holder = new Deki::Object("Holder");
        holder->SetX(0.25f);
        holder->SetY(-0.125f);
        holder->SetScale(1.0f, 1.0f);
        scene.AddObject(holder);
        if (batched)
        {
            batch = holder->AddComponent<SpriteBatchComponent>();
            batch->SetTypeInfo(&kBatchInfo);
            batch->sprite = sheet;
            for (const PieceSpec& p : kPieces)
            {
                const int32_t i = batch->Add(p.x, p.y, p.frame * 8, 0, 8, 8, p.order);
                batch->SetFlip(static_cast<uint32_t>(i), p.flip, false);
            }
        }
        else
        {
            for (const PieceSpec& p : kPieces)
            {
                auto* o = new Deki::Object("Piece");
                o->SetX(p.x);
                o->SetY(p.y);
                holder->AddChild(o);
                auto* s = o->AddComponent<SpriteComponent>();
                s->SetTypeInfo(&kSpriteInfo);
                s->sprite = sheet;
                s->SetFrameRect(p.frame * 8, 0, 8, 8);
                s->sortingOrder = p.order;
                s->flipHorizontal = p.flip;
                sprites.push_back(s);
            }
        }
        // Something else, sorted between the pieces.
        auto* w = new Deki::Object("Walker");
        w->SetX(0.0f);
        w->SetY(0.2f);
        auto* ws = w->AddComponent<SpriteComponent>();
        ws->SetTypeInfo(&kSpriteInfo);
        ws->sprite = walker;
        ws->sortingOrder = 22;
        scene.AddObject(w);
    }

    void MovePiece(uint32_t i, float x, float y, int order)
    {
        if (batch)
        {
            batch->SetPlace(i, x, y, order);
        }
        else
        {
            sprites[i]->GetOwner()->SetX(x);
            sprites[i]->GetOwner()->SetY(y);
            sprites[i]->sortingOrder = order;
        }
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

    // Redraws only what changed into `frame`, as Redraw Changes does.
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
}  // namespace

TEST(SpriteBatch, PiecesDrawAsSpriteObjectsWould)
{
    auto sheet = Sheet();
    std::unique_ptr<Sprite> walker(Sprite::CreateSolidRGBA(6, 10, 250, 250, 0, 200));
    ASSERT_TRUE(sheet && walker);
    World objects(sheet.get(), walker.get(), false);
    World batched(sheet.get(), walker.get(), true);
    const std::vector<uint8_t> expected = objects.Whole();
    std::vector<uint8_t> empty(expected.size());
    objects.Clear(empty, Deki::Rect{ 0, 0, kW, kH });
    size_t drawn = 0;
    for (size_t i = 0; i < expected.size(); i += 2)
    {
        drawn += (expected[i] != empty[i] || expected[i + 1] != empty[i + 1]) ? 1 : 0;
    }
    EXPECT_GT(drawn, 300u) << "the pieces and the walker are on screen";
    EXPECT_EQ(batched.Whole(), expected);

    // Turned and scaled with its object, as children are.
    objects.holder->SetRotation(0.3f);
    objects.holder->SetScale(1.5f, 0.75f);
    batched.holder->SetRotation(0.3f);
    batched.holder->SetScale(1.5f, 0.75f);
    EXPECT_EQ(batched.Whole(), objects.Whole());
}

TEST(SpriteBatch, APieceThatMovesRedrawsAsAnObjectWould)
{
    auto sheet = Sheet();
    std::unique_ptr<Sprite> walker(Sprite::CreateSolidRGBA(6, 10, 250, 250, 0, 200));
    ASSERT_TRUE(sheet && walker);
    World objects(sheet.get(), walker.get(), false);
    World batched(sheet.get(), walker.get(), true);

    std::vector<uint8_t> frame = batched.Whole();
    batched.Changes(frame, 1);  // the first: everything
    frame = batched.Whole();
    batched.Changes(frame, 2);  // nothing changed
    EXPECT_EQ(frame, objects.Whole());

    uint32_t id = 3;
    const float moves[][3] = { { 0.1f, 0.45f, 21 }, { -0.3f, 0.2f, 35 }, { 0.6f, -0.1f, 3 } };
    for (const auto& m : moves)
    {
        objects.MovePiece(2, m[0], m[1], static_cast<int>(m[2]));
        batched.MovePiece(2, m[0], m[1], static_cast<int>(m[2]));
        batched.Changes(frame, id++);
        EXPECT_EQ(frame, objects.Whole()) << "after a piece moved to " << m[0] << ", " << m[1];
    }

    // Hidden, then shown with another frame.
    batched.batch->SetVisible(4, false);
    objects.sprites[4]->GetOwner()->SetActive(false);
    batched.Changes(frame, id++);
    EXPECT_EQ(frame, objects.Whole());
    batched.batch->SetVisible(4, true);
    batched.batch->SetFrameRect(4, 0, 0, 8, 8);
    objects.sprites[4]->GetOwner()->SetActive(true);
    objects.sprites[4]->SetFrameRect(0, 0, 8, 8);
    batched.Changes(frame, id++);
    EXPECT_EQ(frame, objects.Whole());
}
