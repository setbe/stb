#pragma once

// ------------------- Freestanding-friendly Includes -------------------------
#include <stddef.h> // size_t
#include <stdint.h> // uint32_t

namespace stbtt {
    namespace detail {
        struct Edge {
            float x0, y0, x1, y1;
            uint8_t invert;
            std::uint8_t _padding[3]{};

            // Helper:  e[i].y0  <  e[o].y0
            static inline bool CompareY0(Edge* e, size_t i, size_t o) noexcept {
                return e[i].y0 < e[o].y0;
            }
        };


        struct ActiveEdge {
            ActiveEdge* next{};
            float fx{}, fdx{}, fdy{};
            float direction{};
            float sy{};
            float ey{};

            static inline void InitFromEdge(ActiveEdge& z, const Edge& e,
                    int off_x, float start_point) noexcept;

            void HandleClipped(float* scanline, int x,
                    float x0, float y0, float x1, float y1) const noexcept;
            
            void FillActiveEdges(float* scanline, float* scanline_fill,
                    int len, float y_top) noexcept;
        }; // struct ActiveEdge


        void ActiveEdge::InitFromEdge(ActiveEdge& z, const Edge& e,
                int off_x, float start_point) noexcept {
            const float dxdy = (e.x1 - e.x0) / (e.y1 - e.y0);
            z.fdx = dxdy;
            z.fdy = dxdy!=0.f ? 1.f/dxdy : 0.f;
            z.fx  = (e.x0 + dxdy * (start_point - e.y0)) - static_cast<float>(off_x);
            z.direction = e.invert ? 1.f : -1.f;
            z.sy = e.y0;
            z.ey = e.y1;
            z.next = nullptr;
        }

        void ActiveEdge::HandleClipped(float* scanline, int x,
                                       float x0, float y0, 
                                       float x1, float y1) const noexcept {
            const ActiveEdge& e = *this;

            if (y0 == y1) return;
            STBTT_assert(y0 < y1);
            STBTT_assert(e.sy <= e.ey);
            if (y0 > e.ey) return;
            if (y1 < e.sy) return;
            if (y0 < e.sy) {
                x0 += (x1 - x0) * (e.sy - y0) / (y1 - y0);
                y0 = e.sy;
            }
            if (y1 > e.ey) {
                x1 += (x1 - x0) * (e.ey - y1) / (y1 - y0);
                y1 = e.ey;
            }

            if (x0 == x)            STBTT_assert(x1 <= x + 1);
            else if (x0 == x + 1)   STBTT_assert(x1 >= x);
            else if (x0 <= x)       STBTT_assert(x1 <= x);
            else if (x0 >= x + 1)   STBTT_assert(x1 >= x + 1);
            else                    STBTT_assert(x1 >= x && x1 <= x + 1);

            if (x0 <= x && x1 <= x) scanline[x] += e.direction * (y1 - y0);
            else if (x0 >= x + 1 && x1 >= x + 1);
            else {
                STBTT_assert(x0 >= x && x0 <= x + 1 && x1 >= x && x1 <= x + 1);
                scanline[x] += e.direction * (y1 - y0) * (1 - ((x0 - x) + (x1 - x)) / 2); // coverage = 1 - average x position
            }
        }


        void ActiveEdge::FillActiveEdges(float* scanline, float* scanline_fill,
                               int len, float y_top) noexcept {

            auto SizedTrapezoidArea = [](float h, float top_w, float bottom_w) noexcept {
                STBTT_assert(top_w >= 0 && bottom_w >= 0);
                return (top_w + bottom_w) / 2.f * h;
            };
            auto PositionTrapezoidArea = [SizedTrapezoidArea](float h, float tx0, float tx1, float bx0, float bx1) noexcept {
                return SizedTrapezoidArea(h, tx1 - tx0, bx1 - bx0);
            };
            auto SizedTriangleArea = [](float h, float w) noexcept { return h * w / 2; };

            float y_bottom = y_top + 1;
            ActiveEdge* e = this;

            while (e) {
                // brute force every pixel

                // compute intersection points with top & bottom
                STBTT_assert(e->ey >= y_top);

                if (e->fdx == 0) {
                    float x0 = e->fx;
                    if (x0 < len) {
                        if (x0 >= 0) {
                            e->HandleClipped(scanline, static_cast<int>(x0), x0, y_top, x0, y_bottom);
                            e->HandleClipped(scanline_fill - 1, static_cast<int>(x0 + 1), x0, y_top, x0, y_bottom);
                        }
                        else {
                            e->HandleClipped(scanline_fill - 1, 0, x0, y_top, x0, y_bottom);
                        }
                    }
                }
                else {
                    float x0 = e->fx;
                    float dx = e->fdx;
                    float xb = x0 + dx;
                    float x_top, x_bottom;
                    float sy0, sy1;
                    float dy = e->fdy;
                    STBTT_assert(e->sy <= y_bottom && e->ey >= y_top);

                    // compute endpoints of line segment clipped to this scanline (if the
                    // line segment starts on this scanline. x0 is the intersection of the
                    // line with y_top, but that may be off the line segment.
                    if (e->sy > y_top) {
                        x_top = x0 + dx * (e->sy - y_top);
                        sy0 = e->sy;
                    }
                    else {
                        x_top = x0;
                        sy0 = y_top;
                    }
                    if (e->ey < y_bottom) {
                        x_bottom = x0 + dx * (e->ey - y_top);
                        sy1 = e->ey;
                    }
                    else {
                        x_bottom = xb;
                        sy1 = y_bottom;
                    }

                    if (x_top >= 0 && x_bottom >= 0 && x_top < len && x_bottom < len) {
                        // from here on, we don't have to range check x values

                        if (static_cast<int>(x_top) == static_cast<int>(x_bottom)) {
                            float height;
                            // simple case, only spans one pixel
                            int x = static_cast<int>(x_top);
                            height = (sy1 - sy0) * e->direction;
                            STBTT_assert(x >= 0 && x < len);
                            scanline[x] += PositionTrapezoidArea(height, x_top, x + 1.0f, x_bottom, x + 1.0f);
                            scanline_fill[x] += height; // everything right of this pixel is filled
                        }
                        else {
                            int x, x1, x2;
                            float y_crossing, y_final, step, sign, area;
                            // covers 2+ pixels
                            if (x_top > x_bottom) {
                                // flip scanline vertically; signed area is the same
                                float t;
                                sy0 = y_bottom - (sy0 - y_top);
                                sy1 = y_bottom - (sy1 - y_top);
                                t = sy0, sy0 = sy1, sy1 = t;
                                t = x_bottom, x_bottom = x_top, x_top = t;
                                dx = -dx;
                                dy = -dy;
                                t = x0, x0 = xb, xb = t;
                            }
                            STBTT_assert(dy >= 0);
                            STBTT_assert(dx >= 0);

                            x1 = static_cast<int>(x_top);
                            x2 = static_cast<int>(x_bottom);
                            // compute intersection with y axis at x1+1
                            y_crossing = y_top + dy * (x1 + 1 - x0);

                            // compute intersection with y axis at x2
                            y_final = y_top + dy * (x2 - x0);

                            //           x1    x_top                            x2    x_bottom
                            //     y_top  +------|-----+------------+------------+--------|---+------------+
                            //            |            |            |            |            |            |
                            //            |            |            |            |            |            |
                            //       sy0  |      Txxxxx|............|............|............|............|
                            // y_crossing |            *xxxxx.......|............|............|............|
                            //            |            |     xxxxx..|............|............|............|
                            //            |            |     /-   xx*xxxx........|............|............|
                            //            |            | dy <       |    xxxxxx..|............|............|
                            //   y_final  |            |     \-     |          xx*xxx.........|............|
                            //       sy1  |            |            |            |   xxxxxB...|............|
                            //            |            |            |            |            |            |
                            //            |            |            |            |            |            |
                            //  y_bottom  +------------+------------+------------+------------+------------+
                            //
                            // goal is to measure the area covered by '.' in each pixel

                            // if x2 is right at the right edge of x1, y_crossing can blow up, github #1057
                            // @TODO: maybe test against sy1 rather than y_bottom?
                            if (y_crossing > y_bottom)
                                y_crossing = y_bottom;

                            sign = e->direction;

                            // area of the rectangle covered from sy0..y_crossing
                            area = sign * (y_crossing - sy0);

                            // area of the triangle (x_top,sy0), (x1+1,sy0), (x1+1,y_crossing)
                            scanline[x1] += SizedTriangleArea(area, x1 + 1 - x_top);

                            // check if final y_crossing is blown up; no test case for this
                            if (y_final > y_bottom) {
                                y_final = y_bottom;
                                dy = (y_final - y_crossing) / (x2 - (x1 + 1)); // if denom=0, y_final = y_crossing, so y_final <= y_bottom
                            }

                            // in second pixel, area covered by line segment found in first pixel
                            // is always a rectangle 1 wide * the height of that line segment; this
                            // is exactly what the variable 'area' stores. it also gets a contribution
                            // from the line segment within it. the THIRD pixel will get the first
                            // pixel's rectangle contribution, the second pixel's rectangle contribution,
                            // and its own contribution. the 'own contribution' is the same in every pixel except
                            // the leftmost and rightmost, a trapezoid that slides down in each pixel.
                            // the second pixel's contribution to the third pixel will be the
                            // rectangle 1 wide times the height change in the second pixel, which is dy.

                            step = sign * dy * 1; // dy is dy/dx, change in y for every 1 change in x,
                            // which multiplied by 1-pixel-width is how much pixel area changes for each step in x
                            // so the area advances by 'step' every time

                            for (x = x1 + 1; x < x2; ++x) {
                                scanline[x] += area + step / 2; // area of trapezoid is 1*step/2
                                area += step;
                            }
                            STBTT_assert(STBTT_fabs(area) <= 1.01f); // accumulated error from area += step unless we round step down
                            STBTT_assert(sy1 > y_final - 0.01f);

                            // area covered in the last pixel is the rectangle from all the pixels to the left,
                            // plus the trapezoid filled by the line segment in this pixel all the way to the right edge
                            scanline[x2] += area + sign * PositionTrapezoidArea(sy1 - y_final, static_cast<float>(x2), x2 + 1.0f, x_bottom, x2 + 1.0f);

                            // the rest of the line is filled based on the total height of the line segment in this pixel
                            scanline_fill[x2] += sign * (sy1 - sy0);
                        }
                    }
                    else {
                        // if edge goes outside of box we're drawing, we require
                        // clipping logic. since this does not match the intended use
                        // of this library, we use a different, very slow brute
                        // force implementation
                        // note though that this does happen some of the time because
                        // x_top and x_bottom can be extrapolated at the top & bottom of
                        // the shape and actually lie outside the bounding box
                        int x;
                        for (x = 0; x < len; ++x) {
                            // cases:
                            //
                            // there can be up to two intersections with the pixel. any intersection
                            // with left or right edges can be handled by splitting into two (or three)
                            // regions. intersections with top & bottom do not necessitate case-wise logic.
                            //
                            // the old way of doing this found the intersections with the left & right edges,
                            // then used some simple logic to produce up to three segments in sorted order
                            // from top-to-bottom. however, this had a problem: if an x edge was epsilon
                            // across the x border, then the corresponding y position might not be distinct
                            // from the other y segment, and it might ignored as an empty segment. to avoid
                            // that, we need to explicitly produce segments based on x positions.

                            // rename variables to clearly-defined pairs
                            float y0 = y_top;
                            float x1 = static_cast<float>(x);
                            float x2 = static_cast<float>(x + 1);
                            float x3 = xb;
                            float y3 = y_bottom;

                            // x = e->x + e->dx * (y-y_top)
                            // (y-y_top) = (x - e->x) / e->dx
                            // y = (x - e->x) / e->dx + y_top
                            float y1 = (x - x0) / dx + y_top;
                            float y2 = (x + 1 - x0) / dx + y_top;

                            if (x0 < x1 && x3 > x2) {         // three segments descending down-right
                                e->HandleClipped(scanline, x, x0, y0, x1, y1);
                                e->HandleClipped(scanline, x, x1, y1, x2, y2);
                                e->HandleClipped(scanline, x, x2, y2, x3, y3);
                            }
                            else if (x3 < x1 && x0 > x2) {  // three segments descending down-left
                                e->HandleClipped(scanline, x, x0, y0, x2, y2);
                                e->HandleClipped(scanline, x, x2, y2, x1, y1);
                                e->HandleClipped(scanline, x, x1, y1, x3, y3);
                            }
                            else if (x0 < x1 && x3 > x1) {  // two segments across x, down-right
                                e->HandleClipped(scanline, x, x0, y0, x1, y1);
                                e->HandleClipped(scanline, x, x1, y1, x3, y3);
                            }
                            else if (x3 < x1 && x0 > x1) {  // two segments across x, down-left
                                e->HandleClipped(scanline, x, x0, y0, x1, y1);
                                e->HandleClipped(scanline, x, x1, y1, x3, y3);
                            }
                            else if (x0 < x2 && x3 > x2) {  // two segments across x+1, down-right
                                e->HandleClipped(scanline, x, x0, y0, x2, y2);
                                e->HandleClipped(scanline, x, x2, y2, x3, y3);
                            }
                            else if (x3 < x2 && x0 > x2) {  // two segments across x+1, down-left
                                e->HandleClipped(scanline, x, x0, y0, x2, y2);
                                e->HandleClipped(scanline, x, x2, y2, x3, y3);
                            }
                            else {  // one segment
                                e->HandleClipped(scanline, x, x0, y0, x3, y3);
                            }
                        }
                    }
                }
                e = e->next;
            }
        } // FillActiveEdges
    } // namespace detail
} // namespace stbtt