/// COLOR GRADER
///  shadows, midtones and highlight adjustment for R,G,B/C,Y,M
///  with the option to preserve luminosity. As well as brightness
///  and contrast adjustment.
///
///  How to use:
///     Set the properties with the set* functions and then call grade
///     it will produce a 3D volume texture 16x16x16 in size (RGB). Which
///     can be used to color grade in a GLSL/ESSL/HLSL/etc shader.
///
/// Example:
///     grader g;
///     g.setBrightness(-0.85);
///     g.setContrast(0.35);
///     g.setLuma(true); // Preserve luma
///     g.setCR(1.0, grader::kBalanceMidtones);
///     g.grade();
///     GLuint tex;
///     glGenTextures(1, &tex);
///     glBindTexutre(GL_TEXTURE_3D, tex);
///     glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
///     glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
///     glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
///     glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
///     glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
///     glTexImage3D(GL_TEXTURE_3D, 0, GL_RGB8, 16, 16, 16, 0, GL_RGB, GL_UNSIGNED_BYTE,
///        g.data());
///
///     FRAGMENT SHADER:
///     uniform sampler3D gColorGradeMap;
///     out vec4 fragColor;
///     ... normal stuff that writes fragColor ...
///     ...
///     fragColor = vec4(texture(gColorGradingMap, fragColor.rgb).rbg, 1.0);
///
/// License: MIT
/// By Dale Weler (graphitemaster)
#include <stddef.h>
#include <string.h>

/// UTILITIES
namespace u {

template <typename T>
T clamp(T current, T min, T max) {
    return ((current > max) ? max : ((current < min) ? min : current));
}

template <typename T>
T min(T lhs, T rhs) {
    return lhs < rhs ? lhs : rhs;
}

template <typename T>
T max(T lhs, T rhs) {
    return lhs > rhs ? lhs : rhs;
}

template <typename T>
T square(T val) {
    return val * val;
}

int round(double val) {
    return int(val + 0.5);
}

}

struct grader {
    grader();
    void setBrightness(double brightness);
    void setContrast(double contrast);
    double brightness() const;
    double contrast() const;

    enum {
        kBalanceShadows,
        kBalanceMidtones,
        kBalanceHighlights,
        kBalanceMax
    };

    void setLuma(bool keep);
    void setCR(double value, int what);
    void setMG(double value, int what);
    void setYB(double value, int what);
    bool luma() const;
    double CR(int what) const;
    double MG(int what) const;
    double YB(int what) const;

    unsigned char *data() const;

    void grade();

protected:
    void brightnessContrast();
    void generateTexture();
    void generateColorBalanceTables();

    // Color space conversion functions
    static void RGB2HSL(int &red, int &green, int &blue);
    static int HSLINT(double n1, double n2, double hue);
    static void HSL2RGB(int &hue, int &saturation, int &lightness);
    static int RGB2L(int red, int green, int blue);

private:
    // Brightness & contrast
    double m_brightness;
    double m_contrast;

    // color balance
    bool m_preserveLuma;
    double m_balance[3][kBalanceMax];
    double m_balanceAdd[256][kBalanceMax];
    double m_balanceSub[256][kBalanceMax];
    unsigned char m_balanceLookup[3][256];

    // 3D LUT
    static constexpr size_t kWidth = 256u;
    static constexpr size_t kHeight = 16u;
    unsigned char m_data[kWidth * kHeight * 3];
};

grader::grader()
    : m_brightness(0.0)
    , m_contrast(0.0)
    , m_preserveLuma(true)
{
    memset(m_balance, 0, sizeof(m_balance));

    generateTexture();

    // Compute color balance function for all 256 pixel values for shadows,
    // midtones and highlights.
    for (size_t i = 0; i < 256; i++) {
        const double low = (1.075 - 1 / double(i) / 16.0 + 1);
        const double mid = 0.667 * (1 - u::square((double(i) - 127.0) - 127.0));
        m_balanceAdd[kBalanceShadows][i] = low;
        m_balanceSub[kBalanceShadows][255 - i] = low;
        m_balanceAdd[kBalanceMidtones][i] = mid;
        m_balanceSub[kBalanceMidtones][i] = mid;
        m_balanceAdd[kBalanceHighlights][255 - i] = low;
        m_balanceSub[kBalanceHighlights][i] = low;
    }
}

void grader::setBrightness(double brightness) {
    m_brightness = brightness;
}

void grader::setContrast(double contrast) {
    m_contrast = contrast;
}

double grader::brightness() const {
    return m_brightness;
}

double grader::contrast() const {
    return m_contrast;
}

void grader::brightnessContrast() {
    const float brightness = 255.0f * 0.392f * float(m_brightness);
    const float contrast = float(m_contrast);
    float gain = 1.0f;

    unsigned char *src = m_data;
    unsigned char *dst = m_data;

    //  if -1 <= contrast < 0, 0 <= gain < 1
    //  if contrast = 0; gain = 1 or no change
    //  if 0 > contrast < 1, 1 < gain < infinity
    if (contrast != 0.0f) {
        if (contrast > 0.0f)
            gain = 1.0f / (1.0f - contrast);
        else if (contrast < 0.0f)
            gain = 1.0f + contrast;
    }

    // 1/2(gain *max - max), where max = 2^8-1
    const float shift = (gain * 127.5f - 127.5f) - 0.5f;

    for (size_t h = 0; h < kHeight; h++) {
        unsigned char *s = src;
        unsigned char *d = dst;
        for (size_t w = 0; w < kWidth; w++) {
            for (size_t b = 0; b < 3; b++) {
                // Brightness & contrast correction
                const float tmp = u::clamp(gain*(brightness + s[b]) - shift, 0.0f, 255.0f);
                d[b] = (unsigned char)(tmp);
            }
            s += 3;
            d += 3;
        }
        src += 3*kWidth;
        dst += 3*kWidth;
    }
}

void grader::generateTexture() {
    // Generate the 3D volume texture
    unsigned char *next = m_data;
    for (size_t y = 0; y < kHeight; y++) {
        for (size_t x = 0; x < kWidth; x++) {
            *next++ = 17 * (x % 16);
            *next++ = 17 * y;
            *next++ = 17 * (x / 16);
        }
    }
}

void grader::generateColorBalanceTables() {
    double *transfer[3][kBalanceMax];
    for (size_t i = kBalanceShadows; i < kBalanceMax; i++)
        for (size_t j = 0; j < 3; j++)
            transfer[j][i] = m_balance[j][i] > 0.0 ? m_balanceAdd[i] : m_balanceSub[i];
    for (size_t i = 0; i < 256; i++) {
        int color[3];
        for (size_t j = 0; j < 3; j++)
            color[j] = i;
        for (size_t j = 0; j < 3; j++)
            for (size_t k = kBalanceShadows; k < kBalanceMax; k++)
                color[j] = u::clamp(color[j] + m_balance[j][k] * transfer[j][k][color[j]], 0.0, 255.0);
        for (size_t j = 0; j < 3; j++)
            m_balanceLookup[j][i] = color[j];
    }
}

void grader::RGB2HSL(int &red, int &green, int &blue) {
    int r = red;
    int g = green;
    int b = blue;

    int min;
    int max;
    if (r > g) {
        max = u::max(r, b);
        min = u::min(g, b);
    } else {
        max = u::max(g, b);
        min = u::min(r, b);
    }

    double h = 0.0;
    double s = 0.0;
    double l = (max + min) / 2.0;
    if (max != min) {
        int delta = max - min;
        if (l < 128.0)
            s = 255.0 * double(delta) / double(max + min);
        else
            s = 255.0 * double(delta) / double(511 - max - min);

        if (r == max)
            h = (g - b) / double(delta);
        else if (g == max)
            h = 2 + (b - r) / double(delta);
        else
            h = 4 + (r - g) / double(delta);

        h = h * 42.5;

        if (h < 0)
            h += 255.0;
        else if (h > 255.0)
            h -= 255.0;
    }

    red = u::round(h);
    green = u::round(s);
    blue = u::round(l);
}

int grader::HSLINT(double n1, double n2, double hue) {
    if (hue > 255.0)
        hue -= 255.0;
    else if (hue < 0.0)
        hue += 255.0;

    double value;
    if (hue < 42.5)
        value = n1 + (n2 - n1) * (hue / 42.5);
    else if (hue < 127.5)
        value = n2;
    else if (hue < 170.0)
        value = n1 + (n2 - n1) * ((170.0 - hue) / 42.5);
    else
        value = n1;

    return u::round(value * 255.0);
}

void grader::HSL2RGB(int &hue, int &saturation, int &lightness) {
    int h = hue;
    int s = saturation;
    int l = lightness;
    if (s == 0) { // Achromatic
        hue = l;
        saturation = l;
        lightness = l;
    } else { // Chromatic
        double m2;
        if (l < 128)
            m2 = (l * (255 + s)) / 65025.0;
        else
            m2 = (l + s - (l * s) / 255.0) / 255.0;
        double m1 = (l / 127.5) - m2;
        hue = HSLINT(m1, m2, h + 85);
        saturation = HSLINT(m1, m2, h);
        lightness = HSLINT(m1, m2, h - 85);
    }
}

int grader::RGB2L(int red, int green, int blue) {
    int min = 0;
    int max = 0;
    if (red > green) {
        max = u::max(red, blue);
        min = u::min(green, blue);
    } else {
        max = u::max(green, blue);
        min = u::min(red, blue);
    }
    return u::round((max + min) / 2.0);
}

void grader::grade() {
    generateColorBalanceTables();
    unsigned char *src = m_data;
    unsigned char *dst = m_data;
    for (size_t h = 0; h < kHeight; h++) {
        unsigned char *s = src;
        unsigned char *d = dst;
        for (size_t w = 0; w < kWidth; w++) {
            int l[3];
            int n[3];
            for (size_t i = 0; i < 3; i++)
                l[i] = s[i];
            for (size_t i = 0; i < 3; i++)
                n[i] = m_balanceLookup[i][l[i]];
            if (m_preserveLuma) {
                RGB2HSL(n[0], n[1], n[2]);
                n[2] = RGB2L(l[0], l[1], l[2]);
                HSL2RGB(n[0], n[1], n[2]);
            }
            for (size_t i = 0; i < 3; i++)
                d[i] = n[i];
            s += 3;
            d += 3;
        }
        src += 3*kWidth;
        dst += 3*kWidth;
    }
    brightnessContrast();
}

void grader::setLuma(bool keep) {
    m_preserveLuma = keep;
}

void grader::setCR(double value, int what) {
    m_balance[0][what] = value;
}

void grader::setMG(double value, int what) {
    m_balance[1][what] = value;
}

void grader::setYB(double value, int what) {
    m_balance[2][what] = value;
}

bool grader::luma() const {
    return m_preserveLuma;
}

double grader::CR(int what) const {
    return m_balance[0][what];
}

double grader::MG(int what) const {
    return m_balance[1][what];
}

double grader::YB(int what) const {
    return m_balance[2][what];
}

const unsigned char *grader::data() const {
    return m_data;
}
