// Launcher sous Linux : le sous-ensemble de Direct2D 1.1 et de DirectWrite dont se sert dessin.cpp, réalisé sur cairo
// (dessin) et pango (textes). Le dessin du launcher (dessin.cpp) reste celui de Windows : mêmes appels, mêmes
// coordonnées en DIP (1/96 de pouce), même échelle d'affichage (g.dpi). Seuls la création de l'appareil et la
// présentation (chaîne d'échange DXGI → surface de la fenêtre SDL) diffèrent, dans dessin.cpp.
//
// Effets : la matrice de couleur (étalonnage de la vidéo de fond) et le flou gaussien (lueur du titre) sont calculés
// sur le processeur, une fois par nouvelle image d'entrée (la vidéo change 30 fois par seconde au plus, le titre une
// fois). Polices : celles demandées (Bahnschrift, Cascadia Mono) sont absentes de Linux : Barlow (fournie avec le paquet,
// licence OFL, largeurs proches) et DejaVu Sans Mono les remplacent, par la liste de familles de pango.
#pragma once
#ifdef _WIN32
#error "d2d_cairo.h : Linux seulement"
#endif

#include <cairo.h>
#include <pango/pangocairo.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

// ---------------------------------------------------------------- types de base
struct D2D1_POINT_2F { float x, y; };
struct D2D1_RECT_F { float left, top, right, bottom; };
struct D2D1_COLOR_F { float r, g, b, a; };
struct D2D1_SIZE_U { UINT32 width, height; };
struct D2D1_ELLIPSE { D2D1_POINT_2F point; float radiusX, radiusY; };
struct D2D1_ROUNDED_RECT { D2D1_RECT_F rect; float radiusX, radiusY; };
struct D2D1_GRADIENT_STOP { float position; D2D1_COLOR_F color; };
struct D2D1_BEZIER_SEGMENT { D2D1_POINT_2F point1, point2, point3; };
struct D2D1_QUADRATIC_BEZIER_SEGMENT { D2D1_POINT_2F point1, point2; };
struct D2D1_MATRIX_3X2_F { float _11, _12, _21, _22, _31, _32; };
struct D2D1_MATRIX_5X4_F {
    float _11, _12, _13, _14, _21, _22, _23, _24, _31, _32, _33, _34, _41, _42, _43, _44, _51, _52, _53, _54;
};
inline D2D1_MATRIX_3X2_F operator*(const D2D1_MATRIX_3X2_F& a, const D2D1_MATRIX_3X2_F& b) {   // a puis b
    return {a._11 * b._11 + a._12 * b._21, a._11 * b._12 + a._12 * b._22, a._21 * b._11 + a._22 * b._21,
            a._21 * b._12 + a._22 * b._22, a._31 * b._11 + a._32 * b._21 + b._31, a._31 * b._12 + a._32 * b._22 + b._32};
}

enum DXGI_FORMAT { DXGI_FORMAT_UNKNOWN = 0, DXGI_FORMAT_B8G8R8A8_UNORM = 87 };
enum D2D1_ALPHA_MODE { D2D1_ALPHA_MODE_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED, D2D1_ALPHA_MODE_STRAIGHT, D2D1_ALPHA_MODE_IGNORE };
struct D2D1_PIXEL_FORMAT { DXGI_FORMAT format; D2D1_ALPHA_MODE alphaMode; };
enum D2D1_BITMAP_OPTIONS { D2D1_BITMAP_OPTIONS_NONE = 0, D2D1_BITMAP_OPTIONS_TARGET = 1, D2D1_BITMAP_OPTIONS_CANNOT_DRAW = 2 };
inline D2D1_BITMAP_OPTIONS operator|(D2D1_BITMAP_OPTIONS a, D2D1_BITMAP_OPTIONS b) { return (D2D1_BITMAP_OPTIONS)((int)a | (int)b); }
struct D2D1_BITMAP_PROPERTIES { D2D1_PIXEL_FORMAT pixelFormat; float dpiX, dpiY; };
struct D2D1_BITMAP_PROPERTIES1 { D2D1_PIXEL_FORMAT pixelFormat; float dpiX, dpiY; D2D1_BITMAP_OPTIONS bitmapOptions; };
enum D2D1_EXTEND_MODE { D2D1_EXTEND_MODE_CLAMP, D2D1_EXTEND_MODE_WRAP, D2D1_EXTEND_MODE_MIRROR };
enum D2D1_BITMAP_INTERPOLATION_MODE { D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR };
struct D2D1_BITMAP_BRUSH_PROPERTIES {
    D2D1_EXTEND_MODE extendModeX, extendModeY;
    D2D1_BITMAP_INTERPOLATION_MODE interpolationMode;
};
struct D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES { D2D1_POINT_2F startPoint, endPoint; };
struct D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES { D2D1_POINT_2F center, gradientOriginOffset; float radiusX, radiusY; };
enum D2D1_FIGURE_BEGIN { D2D1_FIGURE_BEGIN_FILLED, D2D1_FIGURE_BEGIN_HOLLOW };
enum D2D1_FIGURE_END { D2D1_FIGURE_END_OPEN, D2D1_FIGURE_END_CLOSED };
enum D2D1_FILL_MODE { D2D1_FILL_MODE_ALTERNATE, D2D1_FILL_MODE_WINDING };
enum D2D1_ANTIALIAS_MODE { D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1_ANTIALIAS_MODE_ALIASED };
enum D2D1_LAYER_OPTIONS1 { D2D1_LAYER_OPTIONS1_NONE };
enum D2D1_INTERPOLATION_MODE { D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR, D2D1_INTERPOLATION_MODE_LINEAR };
enum D2D1_COMPOSITE_MODE { D2D1_COMPOSITE_MODE_SOURCE_OVER };
enum D2D1_DRAW_TEXT_OPTIONS { D2D1_DRAW_TEXT_OPTIONS_NONE };
enum D2D1_PROPERTY_TYPE { D2D1_PROPERTY_TYPE_FLOAT = 5, D2D1_PROPERTY_TYPE_MATRIX_5X4 = 17 };
enum { D2D1_COLORMATRIX_PROP_COLOR_MATRIX = 0, D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION = 0 };
#define D2DERR_RECREATE_TARGET ((HRESULT)0x8899000CL)

struct GUID { uint32_t a; uint16_t b, c; uint8_t d[8]; };
inline bool operator==(const GUID& x, const GUID& y) { return !memcmp(&x, &y, sizeof x); }
#define __uuidof(x) nullptr

class ID2D1Geometry;
struct D2D1_LAYER_PARAMETERS1 {
    D2D1_RECT_F contentBounds;
    ID2D1Geometry* geometricMask;
    D2D1_ANTIALIAS_MODE maskAntialiasMode;
    D2D1_MATRIX_3X2_F maskTransform;
    float opacity;
    void* opacityBrush;
    D2D1_LAYER_OPTIONS1 layerOptions;
};

namespace D2D1 {
inline D2D1_POINT_2F Point2F(float x = 0, float y = 0) { return {x, y}; }
inline D2D1_RECT_F RectF(float l = 0, float t = 0, float r = 0, float b = 0) { return {l, t, r, b}; }
inline D2D1_SIZE_U SizeU(UINT32 w = 0, UINT32 h = 0) { return {w, h}; }
inline D2D1_COLOR_F ColorF(float r, float g, float b, float a = 1.f) { return {r, g, b, a}; }
inline D2D1_ELLIPSE Ellipse(D2D1_POINT_2F c, float rx, float ry) { return {c, rx, ry}; }
inline D2D1_ROUNDED_RECT RoundedRect(D2D1_RECT_F r, float rx, float ry) { return {r, rx, ry}; }
inline D2D1_BEZIER_SEGMENT BezierSegment(D2D1_POINT_2F a, D2D1_POINT_2F b, D2D1_POINT_2F c) { return {a, b, c}; }
inline D2D1_QUADRATIC_BEZIER_SEGMENT QuadraticBezierSegment(D2D1_POINT_2F a, D2D1_POINT_2F b) { return {a, b}; }
inline D2D1_MATRIX_3X2_F IdentityMatrix() { return {1, 0, 0, 1, 0, 0}; }
inline D2D1_RECT_F InfiniteRect() { return {-1e30f, -1e30f, 1e30f, 1e30f}; }
inline D2D1_PIXEL_FORMAT PixelFormat(DXGI_FORMAT f = DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE a = D2D1_ALPHA_MODE_UNKNOWN) { return {f, a}; }
inline D2D1_BITMAP_PROPERTIES BitmapProperties(D2D1_PIXEL_FORMAT pf = PixelFormat(), float dx = 96.f, float dy = 96.f) { return {pf, dx, dy}; }
inline D2D1_BITMAP_PROPERTIES1 BitmapProperties1(D2D1_BITMAP_OPTIONS o = D2D1_BITMAP_OPTIONS_NONE, D2D1_PIXEL_FORMAT pf = PixelFormat(),
                                                 float dx = 96.f, float dy = 96.f) { return {pf, dx, dy, o}; }
inline D2D1_BITMAP_BRUSH_PROPERTIES BitmapBrushProperties(D2D1_EXTEND_MODE x = D2D1_EXTEND_MODE_CLAMP, D2D1_EXTEND_MODE y = D2D1_EXTEND_MODE_CLAMP,
                                                          D2D1_BITMAP_INTERPOLATION_MODE m = D2D1_BITMAP_INTERPOLATION_MODE_LINEAR) {
    return {x, y, m};
}
inline D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES LinearGradientBrushProperties(D2D1_POINT_2F a, D2D1_POINT_2F b) { return {a, b}; }
inline D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES RadialGradientBrushProperties(D2D1_POINT_2F c, D2D1_POINT_2F o, float rx, float ry) {
    return {c, o, rx, ry};
}
inline D2D1_LAYER_PARAMETERS1 LayerParameters1(D2D1_RECT_F r = InfiniteRect(), ID2D1Geometry* g = nullptr,
                                               D2D1_ANTIALIAS_MODE aa = D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                               D2D1_MATRIX_3X2_F m = IdentityMatrix(), float op = 1.f, void* br = nullptr,
                                               D2D1_LAYER_OPTIONS1 o = D2D1_LAYER_OPTIONS1_NONE) {
    return {r, g, aa, m, op, br, o};
}
struct Matrix3x2F : D2D1_MATRIX_3X2_F {
    static D2D1_MATRIX_3X2_F Rotation(float deg, D2D1_POINT_2F c = {0, 0}) {   // sens horaire à l'écran
        const float a = deg * 3.14159265358979f / 180.f, co = cosf(a), si = sinf(a);
        return {co, si, -si, co, c.x - c.x * co + c.y * si, c.y - c.x * si - c.y * co};
    }
    static D2D1_MATRIX_3X2_F Scale(float sx, float sy, D2D1_POINT_2F c = {0, 0}) {
        return {sx, 0, 0, sy, c.x - sx * c.x, c.y - sy * c.y};
    }
    static D2D1_MATRIX_3X2_F Translation(float x, float y) { return {1, 0, 0, 1, x, y}; }
};
}  // namespace D2D1

// ---------------------------------------------------------------- objets (compte de références comme COM)
class D2DObjet {
    int rc_ = 1;
public:
    virtual ~D2DObjet() = default;
    unsigned long AddRef() { return ++rc_; }
    unsigned long Release() {
        const int n = --rc_;
        if (!n) delete this;
        return n;
    }
    HRESULT QueryInterface(const void*, void** p) {   // (IDWriteTextLayout1 : la même mise en page)
        AddRef();
        *p = this;
        return S_OK;
    }
};

class ID2D1Image : public D2DObjet {
public:
    // surface à dessiner, échelle (DIP par pixel) et décalage (DIP) de son coin haut gauche ; version : change à
    // chaque nouveau contenu
    virtual cairo_surface_t* surface(float* dip_px, float* dx, float* dy) = 0;
    virtual uint64_t version() const = 0;
};

class ID2D1Bitmap : public ID2D1Image {
public:
    cairo_surface_t* s = nullptr;
    float dpi = 96.f;
    uint64_t ver = 1;
    ~ID2D1Bitmap() override { if (s) cairo_surface_destroy(s); }
    cairo_surface_t* surface(float* k, float* dx, float* dy) override {
        *k = 96.f / dpi, *dx = 0, *dy = 0;
        return s;
    }
    uint64_t version() const override { return ver; }
    HRESULT CopyFromMemory(const void*, const void* px, UINT32 pitch) {
        cairo_surface_flush(s);
        uint8_t* d = cairo_image_surface_get_data(s);
        const int st = cairo_image_surface_get_stride(s), w = cairo_image_surface_get_width(s), h = cairo_image_surface_get_height(s);
        for (int y = 0; y < h; y++) memcpy(d + (size_t)y * st, (const uint8_t*)px + (size_t)y * pitch, (size_t)w * 4);
        cairo_surface_mark_dirty(s);
        ver++;
        return S_OK;
    }
};
typedef ID2D1Bitmap ID2D1Bitmap1;

class ID2D1Brush : public D2DObjet {
public:
    virtual cairo_pattern_t* motif() = 0;   // à libérer par l'appelant
};
class ID2D1SolidColorBrush : public ID2D1Brush {
public:
    D2D1_COLOR_F c{0, 0, 0, 1};
    void SetColor(const D2D1_COLOR_F& v) { c = v; }
    cairo_pattern_t* motif() override { return cairo_pattern_create_rgba(c.r, c.g, c.b, c.a); }
};
class ID2D1GradientStopCollection : public D2DObjet {
public:
    std::vector<D2D1_GRADIENT_STOP> arrets;
};
inline void d2d_arrets(cairo_pattern_t* p, const std::vector<D2D1_GRADIENT_STOP>& v) {
    for (const auto& s : v) cairo_pattern_add_color_stop_rgba(p, s.position, s.color.r, s.color.g, s.color.b, s.color.a);
    cairo_pattern_set_extend(p, CAIRO_EXTEND_PAD);
}
class ID2D1LinearGradientBrush : public ID2D1Brush {
public:
    D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES p{};
    std::vector<D2D1_GRADIENT_STOP> arrets;
    cairo_pattern_t* motif() override {
        cairo_pattern_t* m = cairo_pattern_create_linear(p.startPoint.x, p.startPoint.y, p.endPoint.x, p.endPoint.y);
        d2d_arrets(m, arrets);
        return m;
    }
};
class ID2D1RadialGradientBrush : public ID2D1Brush {
public:
    D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES p{};
    std::vector<D2D1_GRADIENT_STOP> arrets;
    cairo_pattern_t* motif() override {
        const double cx = p.center.x, cy = p.center.y, rx = std::max(p.radiusX, 1e-3f), ry = std::max(p.radiusY, 1e-3f);
        const double ox = cx + p.gradientOriginOffset.x, oy = cy + p.gradientOriginOffset.y;
        cairo_pattern_t* m = cairo_pattern_create_radial(ox, cy + (oy - cy) * rx / ry, 0, cx, cy, rx);
        d2d_arrets(m, arrets);
        cairo_matrix_t mat;   // ellipse : l'axe vertical ramené au rayon horizontal
        cairo_matrix_init(&mat, 1, 0, 0, rx / ry, 0, cy - cy * rx / ry);
        cairo_pattern_set_matrix(m, &mat);
        return m;
    }
};
class ID2D1BitmapBrush : public ID2D1Brush {
public:
    ID2D1Bitmap* bm = nullptr;
    D2D1_BITMAP_BRUSH_PROPERTIES p{};
    ~ID2D1BitmapBrush() override { if (bm) bm->Release(); }
    cairo_pattern_t* motif() override {
        cairo_pattern_t* m = cairo_pattern_create_for_surface(bm->s);
        cairo_pattern_set_extend(m, p.extendModeX == D2D1_EXTEND_MODE_WRAP ? CAIRO_EXTEND_REPEAT : CAIRO_EXTEND_PAD);
        cairo_pattern_set_filter(m, p.interpolationMode == D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR ? CAIRO_FILTER_NEAREST
                                                                                                          : CAIRO_FILTER_BILINEAR);
        cairo_matrix_t mat;   // pixels de l'image → DIP
        cairo_matrix_init_scale(&mat, bm->dpi / 96.0, bm->dpi / 96.0);
        cairo_pattern_set_matrix(m, &mat);
        return m;
    }
};

// ---------------------------------------------------------------- géométries
class ID2D1Geometry : public D2DObjet {
public:
    struct Op { int type; D2D1_POINT_2F p[3]; };   // 0 début, 1 ligne, 2 bézier cubique, 3 quadratique, 4 fin fermée, 5 fin ouverte
    std::vector<Op> ops;
    D2D1_FILL_MODE remplissage = D2D1_FILL_MODE_ALTERNATE;
    void tracer(cairo_t* cr) const {
        cairo_new_path(cr);
        D2D1_POINT_2F cur{0, 0};
        for (const Op& o : ops) {
            switch (o.type) {
            case 0: cairo_new_sub_path(cr); cairo_move_to(cr, o.p[0].x, o.p[0].y); cur = o.p[0]; break;
            case 1: cairo_line_to(cr, o.p[0].x, o.p[0].y); cur = o.p[0]; break;
            case 2: cairo_curve_to(cr, o.p[0].x, o.p[0].y, o.p[1].x, o.p[1].y, o.p[2].x, o.p[2].y); cur = o.p[2]; break;
            case 3: {   // quadratique → cubique
                const D2D1_POINT_2F c = o.p[0], e = o.p[1];
                cairo_curve_to(cr, cur.x + 2.f / 3 * (c.x - cur.x), cur.y + 2.f / 3 * (c.y - cur.y), e.x + 2.f / 3 * (c.x - e.x),
                               e.y + 2.f / 3 * (c.y - e.y), e.x, e.y);
                cur = e;
                break;
            }
            case 4: cairo_close_path(cr); break;
            default: break;
            }
        }
        cairo_set_fill_rule(cr, remplissage == D2D1_FILL_MODE_WINDING ? CAIRO_FILL_RULE_WINDING : CAIRO_FILL_RULE_EVEN_ODD);
    }
};
class ID2D1GeometrySink : public D2DObjet {
public:
    ID2D1Geometry* g = nullptr;
    void SetFillMode(D2D1_FILL_MODE m) { g->remplissage = m; }
    void BeginFigure(D2D1_POINT_2F p, D2D1_FIGURE_BEGIN) { g->ops.push_back({0, {p}}); }
    void AddLine(D2D1_POINT_2F p) { g->ops.push_back({1, {p}}); }
    void AddLines(const D2D1_POINT_2F* p, UINT32 n) { for (UINT32 i = 0; i < n; i++) AddLine(p[i]); }
    void AddBezier(const D2D1_BEZIER_SEGMENT& b) { g->ops.push_back({2, {b.point1, b.point2, b.point3}}); }
    void AddQuadraticBezier(const D2D1_QUADRATIC_BEZIER_SEGMENT& b) { g->ops.push_back({3, {b.point1, b.point2}}); }
    void EndFigure(D2D1_FIGURE_END e) { g->ops.push_back({e == D2D1_FIGURE_END_CLOSED ? 4 : 5, {}}); }
    HRESULT Close() { return S_OK; }
};
class ID2D1PathGeometry : public ID2D1Geometry {
public:
    HRESULT Open(ID2D1GeometrySink** s) {
        auto* k = new ID2D1GeometrySink;
        k->g = this;
        *s = k;
        return S_OK;
    }
};
class ID2D1Factory1 : public D2DObjet {
public:
    HRESULT CreatePathGeometry(ID2D1PathGeometry** g) {
        *g = new ID2D1PathGeometry;
        return S_OK;
    }
};

// ---------------------------------------------------------------- effets (matrice de couleur, flou gaussien)
class ID2D1Effect : public ID2D1Image {
public:
    bool flou = false;
    ID2D1Image* entree = nullptr;
    D2D1_MATRIX_5X4_F mat{};
    float ecart = 3.f;
    cairo_surface_t* sortie = nullptr;
    uint64_t vu = 0;
    const void* vu_entree = nullptr;
    float k = 1, dx = 0, dy = 0;
    ~ID2D1Effect() override {
        if (sortie) cairo_surface_destroy(sortie);
        if (entree) entree->Release();
    }
    void SetInput(UINT32, ID2D1Image* i, BOOL = TRUE) {
        if (i) i->AddRef();
        if (entree) entree->Release();
        entree = i;
        vu = 0;
    }
    HRESULT SetValue(UINT32, D2D1_PROPERTY_TYPE t, const BYTE* v, UINT32 n) {
        if (t == D2D1_PROPERTY_TYPE_MATRIX_5X4 && n == sizeof mat) memcpy(&mat, v, n);
        if (t == D2D1_PROPERTY_TYPE_FLOAT && n == sizeof ecart) memcpy(&ecart, v, n);
        vu = 0;
        return S_OK;
    }
    void GetOutput(ID2D1Image** i) {
        AddRef();
        *i = this;
    }
    uint64_t version() const override { return vu; }
    cairo_surface_t* surface(float* ok, float* odx, float* ody) override {
        if (!entree) return nullptr;
        float ik, idx, idy;
        cairo_surface_t* in = entree->surface(&ik, &idx, &idy);
        if (!in) return nullptr;
        if (!sortie || vu != entree->version() || vu_entree != in) {
            vu = entree->version(), vu_entree = in;
            calculer(in, ik);
            dx = idx - (flou ? marge(ik) * ik : 0), dy = idy - (flou ? marge(ik) * ik : 0), k = ik;
        }
        *ok = k, *odx = dx, *ody = dy;
        return sortie;
    }
private:
    int marge(float ik) const { return (int)ceilf(3.f * ecart / ik); }
    void calculer(cairo_surface_t* in, float ik) {
        cairo_surface_flush(in);
        const int w = cairo_image_surface_get_width(in), h = cairo_image_surface_get_height(in), st = cairo_image_surface_get_stride(in);
        const uint8_t* src = cairo_image_surface_get_data(in);
        if (!flou) {   // matrice de couleur, sur une image sans transparence (vidéo)
            if (!sortie || cairo_image_surface_get_width(sortie) != w || cairo_image_surface_get_height(sortie) != h) {
                if (sortie) cairo_surface_destroy(sortie);
                sortie = cairo_image_surface_create(CAIRO_FORMAT_RGB24, w, h);
            }
            cairo_surface_flush(sortie);
            uint8_t* d = cairo_image_surface_get_data(sortie);
            const int sd = cairo_image_surface_get_stride(sortie);
            const float* c = &mat._11;   // ligne i : canal d'entrée (r, g, b, a, 1) ; colonne j : canal de sortie
            int tab[3][3][256], off[3];
            for (int j = 0; j < 3; j++) {
                off[j] = (int)lroundf(c[16 + j] * 255.f * 256.f);
                for (int i = 0; i < 3; i++)
                    for (int v = 0; v < 256; v++) tab[j][i][v] = (int)lroundf(c[i * 4 + j] * v * 256.f);
            }
            for (int y = 0; y < h; y++) {
                const uint8_t* s = src + (size_t)y * st;
                uint8_t* o = d + (size_t)y * sd;
                for (int x = 0; x < w; x++, s += 4, o += 4) {
                    const int b = s[0], g = s[1], r = s[2];   // BGRA
                    for (int j = 0; j < 3; j++) {
                        const int v = (tab[j][0][r] + tab[j][1][g] + tab[j][2][b] + off[j]) >> 8;
                        o[2 - j] = (uint8_t)std::clamp(v, 0, 255);
                    }
                    o[3] = 255;
                }
            }
            cairo_surface_mark_dirty(sortie);
            return;
        }
        // flou gaussien (≈ trois flous de boîte), image prémultipliée agrandie de 3 écarts de chaque côté
        const int m = marge(ik), W = w + 2 * m, H = h + 2 * m;
        if (sortie) cairo_surface_destroy(sortie);
        sortie = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
        std::vector<float> a((size_t)W * H * 4, 0.f), b(a.size());
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
                for (int c = 0; c < 4; c++) a[(((size_t)(y + m)) * W + x + m) * 4 + c] = src[(size_t)y * st + x * 4 + c];
        const float sigma = ecart / ik;
        const int r = std::max(1, (int)lroundf((sqrtf(12.f * sigma * sigma / 3.f + 1.f) - 1.f) / 2.f));
        auto boite = [&](std::vector<float>& de, std::vector<float>& vers, bool horiz) {
            const int n = horiz ? W : H, lignes = horiz ? H : W;
            const float inv = 1.f / (2 * r + 1);
            for (int l = 0; l < lignes; l++) {
                auto at = [&](int i) -> size_t { return (horiz ? (size_t)l * W + i : (size_t)i * W + l) * 4; };
                float s[4] = {0, 0, 0, 0};
                for (int i = -r; i <= r; i++)
                    if (i >= 0 && i < n)
                        for (int c = 0; c < 4; c++) s[c] += de[at(i) + c];
                for (int i = 0; i < n; i++) {
                    for (int c = 0; c < 4; c++) vers[at(i) + c] = s[c] * inv;
                    const int sort = i - r, entre = i + r + 1;
                    for (int c = 0; c < 4; c++) {
                        if (sort >= 0) s[c] -= de[at(sort) + c];
                        if (entre < n) s[c] += de[at(entre) + c];
                    }
                }
            }
        };
        for (int passe = 0; passe < 3; passe++) boite(a, b, true), boite(b, a, false);
        cairo_surface_flush(sortie);
        uint8_t* d = cairo_image_surface_get_data(sortie);
        const int sd = cairo_image_surface_get_stride(sortie);
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
                for (int c = 0; c < 4; c++) d[(size_t)y * sd + x * 4 + c] = (uint8_t)std::clamp((int)lroundf(a[((size_t)y * W + x) * 4 + c]), 0, 255);
        cairo_surface_mark_dirty(sortie);
    }
};

// ---------------------------------------------------------------- DirectWrite (pango)
enum DWRITE_FONT_WEIGHT { DWRITE_FONT_WEIGHT_LIGHT = 300, DWRITE_FONT_WEIGHT_NORMAL = 400, DWRITE_FONT_WEIGHT_SEMI_BOLD = 600,
                          DWRITE_FONT_WEIGHT_BOLD = 700 };
enum DWRITE_FONT_STYLE { DWRITE_FONT_STYLE_NORMAL };
enum DWRITE_FONT_STRETCH { DWRITE_FONT_STRETCH_NORMAL = 5 };
enum DWRITE_WORD_WRAPPING { DWRITE_WORD_WRAPPING_WRAP, DWRITE_WORD_WRAPPING_NO_WRAP };
enum DWRITE_TRIMMING_GRANULARITY { DWRITE_TRIMMING_GRANULARITY_NONE, DWRITE_TRIMMING_GRANULARITY_CHARACTER };
enum DWRITE_FACTORY_TYPE { DWRITE_FACTORY_TYPE_SHARED };
struct DWRITE_TRIMMING { DWRITE_TRIMMING_GRANULARITY granularity; UINT32 delimiter, delimiterCount; };
struct DWRITE_TEXT_RANGE { UINT32 startPosition, length; };
struct DWRITE_TEXT_METRICS { float left, top, width, widthIncludingTrailingWhitespace, height, layoutWidth, layoutHeight;
                             UINT32 maxBidiReorderingDepth, lineCount; };

class IDWriteInlineObject : public D2DObjet {};
class IDWriteTextFormat : public D2DObjet {
public:
    PangoFontDescription* fd = nullptr;
    bool retour = false, points = false;   // retour à la ligne ; points de suspension au-delà de la largeur
    ~IDWriteTextFormat() override { if (fd) pango_font_description_free(fd); }
    HRESULT SetWordWrapping(DWRITE_WORD_WRAPPING w) { retour = w == DWRITE_WORD_WRAPPING_WRAP; return S_OK; }
    HRESULT SetTrimming(const DWRITE_TRIMMING* t, IDWriteInlineObject* sign) {
        points = t && t->granularity != DWRITE_TRIMMING_GRANULARITY_NONE && sign;
        return S_OK;
    }
};
class IDWriteTextLayout : public D2DObjet {
public:
    PangoLayout* l = nullptr;
    float largeur = 0;
    ~IDWriteTextLayout() override { if (l) g_object_unref(l); }
    HRESULT SetWordWrapping(DWRITE_WORD_WRAPPING w) {
        if (w == DWRITE_WORD_WRAPPING_WRAP) {
            pango_layout_set_ellipsize(l, PANGO_ELLIPSIZE_NONE);
            pango_layout_set_wrap(l, PANGO_WRAP_WORD_CHAR);
        }
        return S_OK;
    }
    HRESULT SetCharacterSpacing(float avant, float apres, float, DWRITE_TEXT_RANGE) {
        PangoAttrList* a = pango_attr_list_new();
        pango_attr_list_insert(a, pango_attr_letter_spacing_new((int)lroundf((avant + apres) * PANGO_SCALE)));
        pango_layout_set_attributes(l, a);
        pango_attr_list_unref(a);
        return S_OK;
    }
    HRESULT GetMetrics(DWRITE_TEXT_METRICS* m) {
        PangoRectangle enc, log;
        pango_layout_get_extents(l, &enc, &log);
        *m = {};
        m->width = m->widthIncludingTrailingWhitespace = (float)log.width / PANGO_SCALE;
        m->height = (float)log.height / PANGO_SCALE;
        m->layoutWidth = largeur;
        m->lineCount = (UINT32)pango_layout_get_line_count(l);
        return S_OK;
    }
};
typedef IDWriteTextLayout IDWriteTextLayout1;

inline PangoContext* d2d_pango() {
    static PangoContext* c = [] {
        PangoContext* x = pango_font_map_create_context(pango_cairo_font_map_get_default());
        pango_cairo_context_set_resolution(x, 96.0);
        cairo_font_options_t* o = cairo_font_options_create();
        cairo_font_options_set_hint_metrics(o, CAIRO_HINT_METRICS_OFF);   // mise en page indépendante de l'échelle
        cairo_font_options_set_hint_style(o, CAIRO_HINT_STYLE_NONE);   // (pango 1.4x : approches régulières)
        cairo_font_options_set_antialias(o, CAIRO_ANTIALIAS_GRAY);
        pango_cairo_context_set_font_options(x, o);
        cairo_font_options_destroy(o);
        return x;
    }();
    return c;
}

inline std::string d2d_utf8(const wchar_t* s, size_t n) {
    std::string o;
    o.reserve(n);
    for (size_t i = 0; i < n; i++) {
        uint32_t c = (uint32_t)s[i];
        if (c < 0x80) o += (char)c;
        else if (c < 0x800) o += (char)(0xC0 | c >> 6), o += (char)(0x80 | (c & 63));
        else if (c < 0x10000) o += (char)(0xE0 | c >> 12), o += (char)(0x80 | ((c >> 6) & 63)), o += (char)(0x80 | (c & 63));
        else o += (char)(0xF0 | c >> 18), o += (char)(0x80 | ((c >> 12) & 63)), o += (char)(0x80 | ((c >> 6) & 63)), o += (char)(0x80 | (c & 63));
    }
    return o;
}

class IDWriteFactory1 : public D2DObjet {
public:
    HRESULT CreateTextFormat(const wchar_t* famille, void*, DWRITE_FONT_WEIGHT poids, DWRITE_FONT_STYLE, DWRITE_FONT_STRETCH,
                             float taille, const wchar_t*, IDWriteTextFormat** f) {
        auto* x = new IDWriteTextFormat;
        std::string fam = d2d_utf8(famille, wcslen(famille));
        if (fam == "Bahnschrift") fam = "Bahnschrift,Barlow,Noto Sans,DejaVu Sans,sans-serif";
        else if (fam == "Cascadia Mono") fam = "Cascadia Mono,DejaVu Sans Mono,Liberation Mono,monospace";
        else if (fam == "Segoe UI") fam = "Noto Sans,DejaVu Sans,sans-serif";
        x->fd = pango_font_description_new();
        pango_font_description_set_family(x->fd, fam.c_str());
        pango_font_description_set_weight(x->fd, (PangoWeight)poids);
        pango_font_description_set_absolute_size(x->fd, taille * PANGO_SCALE);   // en DIP
        *f = x;
        return S_OK;
    }
    HRESULT CreateEllipsisTrimmingSign(IDWriteTextFormat*, IDWriteInlineObject** o) {
        *o = new IDWriteInlineObject;
        return S_OK;
    }
    HRESULT CreateTextLayout(const wchar_t* s, UINT32 n, IDWriteTextFormat* f, float largeur, float, IDWriteTextLayout** out) {
        auto* x = new IDWriteTextLayout;
        x->l = pango_layout_new(d2d_pango());
        x->largeur = largeur;
        pango_layout_set_font_description(x->l, f->fd);
        const std::string u = d2d_utf8(s, n);
        pango_layout_set_text(x->l, u.c_str(), (int)u.size());
        pango_layout_set_width(x->l, (int)std::min(largeur * PANGO_SCALE, 2.0e9f));
        if (f->retour) {
            pango_layout_set_wrap(x->l, PANGO_WRAP_WORD_CHAR);
        } else {
            pango_layout_set_ellipsize(x->l, f->points ? PANGO_ELLIPSIZE_END : PANGO_ELLIPSIZE_NONE);
            if (!f->points) pango_layout_set_width(x->l, -1);
        }
        *out = x;
        return S_OK;
    }
};

// ---------------------------------------------------------------- contexte de dessin
class ID2D1DeviceContext : public D2DObjet {
public:
    ID2D1Bitmap* cible = nullptr;
    cairo_t* cr = nullptr;
    float dpi = 96.f;
    D2D1_MATRIX_3X2_F m = D2D1::IdentityMatrix();
    std::vector<float> calques;   // opacité de chaque PushLayer (< 0 : sans groupe)
    ~ID2D1DeviceContext() override { SetTarget(nullptr); }

    void SetDpi(float x, float) { dpi = x; appliquer(); }
    void SetTarget(ID2D1Image* i) {
        if (cr) cairo_destroy(cr), cr = nullptr;
        if (cible) cible->Release(), cible = nullptr;
        if (!i) return;
        cible = static_cast<ID2D1Bitmap*>(i);
        cible->AddRef();
        cr = cairo_create(cible->s);
        dpi = cible->dpi;
        appliquer();
    }
    void GetTarget(ID2D1Image** i) {
        if (cible) cible->AddRef();
        *i = cible;
    }
    void BeginDraw() {
        calques.clear();
        cairo_reset_clip(cr);
    }
    HRESULT EndDraw() {
        while (!calques.empty()) PopLayer();
        cairo_surface_flush(cible->s);
        cible->ver++;
        return cairo_status(cr) == CAIRO_STATUS_SUCCESS ? S_OK : E_FAIL;
    }
    void SetTransform(const D2D1_MATRIX_3X2_F& v) { m = v; appliquer(); }
    void GetTransform(D2D1_MATRIX_3X2_F* v) { *v = m; }
    void Clear(const D2D1_COLOR_F& c) {
        cairo_save(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_set_source_rgba(cr, c.r, c.g, c.b, c.a);
        cairo_paint(cr);
        cairo_restore(cr);
    }

    // ressources
    HRESULT CreateSolidColorBrush(const D2D1_COLOR_F& c, ID2D1SolidColorBrush** b) {
        *b = new ID2D1SolidColorBrush;
        (*b)->c = c;
        return S_OK;
    }
    HRESULT CreateGradientStopCollection(const D2D1_GRADIENT_STOP* s, UINT32 n, ID2D1GradientStopCollection** c) {
        *c = new ID2D1GradientStopCollection;
        (*c)->arrets.assign(s, s + n);
        return S_OK;
    }
    HRESULT CreateLinearGradientBrush(const D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES& p, ID2D1GradientStopCollection* c,
                                      ID2D1LinearGradientBrush** b) {
        if (!c) return *b = nullptr, E_FAIL;
        *b = new ID2D1LinearGradientBrush;
        (*b)->p = p, (*b)->arrets = c->arrets;
        return S_OK;
    }
    HRESULT CreateRadialGradientBrush(const D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES& p, ID2D1GradientStopCollection* c,
                                      ID2D1RadialGradientBrush** b) {
        if (!c) return *b = nullptr, E_FAIL;
        *b = new ID2D1RadialGradientBrush;
        (*b)->p = p, (*b)->arrets = c->arrets;
        return S_OK;
    }
    HRESULT CreateBitmap(D2D1_SIZE_U t, const void* px, UINT32 pitch, const D2D1_BITMAP_PROPERTIES* p, ID2D1Bitmap** out) {
        return bitmap(t, px, pitch, p ? p->dpiX : 96.f, p && p->pixelFormat.alphaMode == D2D1_ALPHA_MODE_IGNORE, out);
    }
    HRESULT CreateBitmap(D2D1_SIZE_U t, const void* px, UINT32 pitch, const D2D1_BITMAP_PROPERTIES1* p, ID2D1Bitmap** out) {
        return bitmap(t, px, pitch, p ? p->dpiX : 96.f, p && p->pixelFormat.alphaMode == D2D1_ALPHA_MODE_IGNORE, out);
    }
    HRESULT CreateBitmap(D2D1_SIZE_U t, const void* px, UINT32 pitch, const D2D1_BITMAP_PROPERTIES& p, ID2D1Bitmap** out) {
        return CreateBitmap(t, px, pitch, &p, out);
    }
    HRESULT CreateBitmap(D2D1_SIZE_U t, const void* px, UINT32 pitch, const D2D1_BITMAP_PROPERTIES1& p, ID2D1Bitmap** out) {
        return CreateBitmap(t, px, pitch, &p, out);
    }
    HRESULT bitmap(D2D1_SIZE_U t, const void* px, UINT32 pitch, float dpi, bool opaque, ID2D1Bitmap** out) {
        if (!t.width || !t.height) return E_FAIL;
        auto* b = new ID2D1Bitmap;
        b->dpi = dpi > 0 ? dpi : 96.f;
        b->s = cairo_image_surface_create(opaque ? CAIRO_FORMAT_RGB24 : CAIRO_FORMAT_ARGB32, (int)t.width, (int)t.height);
        if (cairo_surface_status(b->s) != CAIRO_STATUS_SUCCESS) {
            b->Release();
            return E_FAIL;
        }
        if (px) b->CopyFromMemory(nullptr, px, pitch);
        *out = b;
        return S_OK;
    }
    HRESULT CreateBitmapBrush(ID2D1Bitmap* bm, const D2D1_BITMAP_BRUSH_PROPERTIES* p, const void*, ID2D1BitmapBrush** b) {
        *b = new ID2D1BitmapBrush;
        bm->AddRef();
        (*b)->bm = bm;
        if (p) (*b)->p = *p;
        return S_OK;
    }
    HRESULT CreateEffect(const GUID& id, ID2D1Effect** e) {
        static const GUID flou = {0x1feb6d69, 0x2fe6, 0x4ac9, {0x8c, 0x58, 0x1d, 0x7f, 0x93, 0xe7, 0xa6, 0xa5}};
        *e = new ID2D1Effect;
        (*e)->flou = id == flou;
        return S_OK;
    }

    // dessin
    void DrawLine(D2D1_POINT_2F a, D2D1_POINT_2F b, ID2D1Brush* br, float ep = 1.f) {
        cairo_new_path(cr);
        cairo_move_to(cr, a.x, a.y);
        cairo_line_to(cr, b.x, b.y);
        trait(br, ep);
    }
    void FillRectangle(const D2D1_RECT_F& r, ID2D1Brush* br) {
        cairo_new_path(cr);
        cairo_rectangle(cr, r.left, r.top, r.right - r.left, r.bottom - r.top);
        remplir(br);
    }
    void DrawRectangle(const D2D1_RECT_F& r, ID2D1Brush* br, float ep = 1.f) {
        cairo_new_path(cr);
        cairo_rectangle(cr, r.left, r.top, r.right - r.left, r.bottom - r.top);
        trait(br, ep);
    }
    void FillEllipse(const D2D1_ELLIPSE& e, ID2D1Brush* br) { ellipse(e); remplir(br); }
    void DrawEllipse(const D2D1_ELLIPSE& e, ID2D1Brush* br, float ep = 1.f) { ellipse(e); trait(br, ep); }
    void FillRoundedRectangle(const D2D1_ROUNDED_RECT& r, ID2D1Brush* br) { arrondi(r); remplir(br); }
    void DrawRoundedRectangle(const D2D1_ROUNDED_RECT& r, ID2D1Brush* br, float ep = 1.f) { arrondi(r); trait(br, ep); }
    void FillGeometry(ID2D1Geometry* g, ID2D1Brush* br, void* = nullptr) {
        if (!g || !br) return;
        g->tracer(cr);
        remplir(br);
    }
    void DrawGeometry(ID2D1Geometry* g, ID2D1Brush* br, float ep = 1.f) {
        if (!g || !br) return;
        g->tracer(cr);
        trait(br, ep);
    }
    void DrawTextLayout(D2D1_POINT_2F p, IDWriteTextLayout* l, ID2D1Brush* br, D2D1_DRAW_TEXT_OPTIONS = D2D1_DRAW_TEXT_OPTIONS_NONE) {
        if (!l || !br) return;
        cairo_pattern_t* mo = br->motif();
        cairo_set_source(cr, mo);
        cairo_new_path(cr);
        cairo_move_to(cr, p.x, p.y);
        pango_cairo_show_layout(cr, l->l);
        cairo_pattern_destroy(mo);
    }
    void DrawImage(ID2D1Image* i, const D2D1_POINT_2F* o = nullptr, const D2D1_RECT_F* = nullptr,
                   D2D1_INTERPOLATION_MODE mode = D2D1_INTERPOLATION_MODE_LINEAR, D2D1_COMPOSITE_MODE = D2D1_COMPOSITE_MODE_SOURCE_OVER) {
        if (!i) return;
        float k, dx, dy;
        cairo_surface_t* s = i->surface(&k, &dx, &dy);
        if (!s) return;
        cairo_save(cr);
        cairo_translate(cr, (o ? o->x : 0) + dx, (o ? o->y : 0) + dy);
        cairo_scale(cr, k, k);
        cairo_set_source_surface(cr, s, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), mode == D2D1_INTERPOLATION_MODE_LINEAR ? CAIRO_FILTER_BILINEAR : CAIRO_FILTER_NEAREST);
        cairo_paint(cr);
        cairo_restore(cr);
    }
    void PushLayer(const D2D1_LAYER_PARAMETERS1* p, void*) {
        cairo_save(cr);
        if (p->geometricMask) {
            p->geometricMask->tracer(cr);
            cairo_clip(cr);
        }
        const bool groupe = p->opacity < 0.999f;
        if (groupe) cairo_push_group(cr);
        calques.push_back(groupe ? p->opacity : -1.f);
    }
    void PopLayer() {
        if (calques.empty()) return;
        const float a = calques.back();
        calques.pop_back();
        if (a >= 0) {
            cairo_pop_group_to_source(cr);
            cairo_paint_with_alpha(cr, a);
        }
        cairo_restore(cr);
    }
    void PushAxisAlignedClip(const D2D1_RECT_F& r, D2D1_ANTIALIAS_MODE) {
        cairo_save(cr);
        cairo_new_path(cr);
        cairo_rectangle(cr, r.left, r.top, r.right - r.left, r.bottom - r.top);
        cairo_clip(cr);
        calques.push_back(-1.f);
    }
    void PopAxisAlignedClip() { PopLayer(); }

private:
    void appliquer() {
        if (!cr) return;
        const double k = dpi / 96.0;
        cairo_matrix_t c;
        cairo_matrix_init(&c, m._11 * k, m._12 * k, m._21 * k, m._22 * k, m._31 * k, m._32 * k);
        cairo_set_matrix(cr, &c);
    }
    void remplir(ID2D1Brush* br) {
        if (!br) return cairo_new_path(cr);
        cairo_pattern_t* mo = br->motif();
        cairo_set_source(cr, mo);
        cairo_fill(cr);
        cairo_pattern_destroy(mo);
    }
    void trait(ID2D1Brush* br, float ep) {
        if (!br) return cairo_new_path(cr);
        cairo_pattern_t* mo = br->motif();
        cairo_set_source(cr, mo);
        cairo_set_line_width(cr, ep);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_MITER);
        cairo_stroke(cr);
        cairo_pattern_destroy(mo);
    }
    void ellipse(const D2D1_ELLIPSE& e) {
        cairo_new_path(cr);
        cairo_save(cr);
        cairo_translate(cr, e.point.x, e.point.y);
        cairo_scale(cr, std::max(e.radiusX, 1e-4f), std::max(e.radiusY, 1e-4f));
        cairo_arc(cr, 0, 0, 1, 0, 2 * M_PI);
        cairo_restore(cr);
    }
    void arrondi(const D2D1_ROUNDED_RECT& r) {
        const double x0 = r.rect.left, y0 = r.rect.top, x1 = r.rect.right, y1 = r.rect.bottom;
        const double rr = std::min({(double)r.radiusX, (x1 - x0) / 2, (y1 - y0) / 2});
        cairo_new_path(cr);
        cairo_new_sub_path(cr);
        cairo_arc(cr, x1 - rr, y0 + rr, rr, -M_PI / 2, 0);
        cairo_arc(cr, x1 - rr, y1 - rr, rr, 0, M_PI / 2);
        cairo_arc(cr, x0 + rr, y1 - rr, rr, M_PI / 2, M_PI);
        cairo_arc(cr, x0 + rr, y0 + rr, rr, M_PI, 1.5 * M_PI);
        cairo_close_path(cr);
    }
};
