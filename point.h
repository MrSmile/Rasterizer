// point.h : 2D vector mathematics
//

#pragma once

#include <cstdint>
#include <ft2build.h>
#include FT_FREETYPE_H



struct Point
{
    int32_t x, y;


    Point() = default;
    constexpr Point(const Point &pt) = default;
    Point &operator = (const Point &pt) = default;

    constexpr Point(int32_t x_, int32_t y_) : x(x_), y(y_)
    {
    }

    constexpr Point(const FT_Vector &v) : x(v.x), y(v.y)
    {
    }


    constexpr Point operator + (const Point &pt)
    {
        return Point(x + pt.x, y + pt.y);
    }

    Point &operator += (const Point &pt)
    {
        x += pt.x;  y += pt.y;  return *this;
    }

    constexpr Point operator - (const Point &pt)
    {
        return Point(x - pt.x, y - pt.y);
    }

    Point &operator -= (const Point &pt)
    {
        x -= pt.x;  y -= pt.y;  return *this;
    }

    constexpr Point operator - () const
    {
        return Point(-x, -y);
    }


    Point &operator *= (int32_t a)
    {
        x *= a;  y *= a;  return *this;
    }

    constexpr Point operator * (int32_t a) const
    {
        return Point(a * x, a * y);
    }

    friend inline constexpr Point operator * (int32_t a, const Point &pt)
    {
        return pt * a;
    }


    constexpr int64_t operator * (const Point &pt) const
    {
        return int64_t(x) * pt.x + int64_t(y) * pt.y;
    }

    constexpr int64_t operator % (const Point &pt) const
    {
        return int64_t(x) * pt.y - int64_t(y) * pt.x;
    }

    constexpr Point operator ~ () const
    {
        return Point(y, -x);
    }


    constexpr Point operator >> (int n) const
    {
        return Point(x >> n, y >> n);
    }

    Point &operator >>= (int n)
    {
        x >>= n;  y >>= n;  return *this;
    }

    constexpr Point operator << (int n) const
    {
        return Point(x << n, y << n);
    }

    Point &operator <<= (int n)
    {
        x <<= n;  y <<= n;  return *this;
    }
};
