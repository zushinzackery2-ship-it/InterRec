#include "pch.h"

#include "PluginVideoRecordDx12Capture.h"

#include <cmath>

namespace
{
    std::uint8_t ConvertUnorm10ToByte(std::uint32_t value)
    {
        return static_cast<std::uint8_t>((value * 255u + 511u) / 1023u);
    }

    float ClampToUnit(float value)
    {
        if (value < 0.0f)
        {
            return 0.0f;
        }

        if (value > 1.0f)
        {
            return 1.0f;
        }

        return value;
    }

    float ConvertHalfToFloat(std::uint16_t value)
    {
        const std::uint32_t sign = static_cast<std::uint32_t>(value & 0x8000u) << 16;
        std::uint32_t exponent = (value >> 10) & 0x1Fu;
        std::uint32_t mantissa = value & 0x03FFu;
        std::uint32_t resultBits = 0;

        if (exponent == 0)
        {
            if (mantissa == 0)
            {
                resultBits = sign;
            }
            else
            {
                exponent = 1;
                while ((mantissa & 0x0400u) == 0)
                {
                    mantissa <<= 1;
                    --exponent;
                }

                mantissa &= 0x03FFu;
                exponent += 112;
                resultBits = sign | (exponent << 23) | (mantissa << 13);
            }
        }
        else if (exponent == 31)
        {
            resultBits = sign | 0x7F800000u | (mantissa << 13);
        }
        else
        {
            exponent += 112;
            resultBits = sign | (exponent << 23) | (mantissa << 13);
        }

        float result = 0.0f;
        CopyMemory(&result, &resultBits, sizeof(result));
        return result;
    }

    std::uint8_t ConvertFloatToSdrByte(float value)
    {
        value = value < 0.0f ? 0.0f : value;

        const float mapped = value / (1.0f + value);
        const float srgb = mapped <= 0.0031308f
            ? mapped * 12.92f
            : 1.055f * std::pow(mapped, 1.0f / 2.4f) - 0.055f;
        return static_cast<std::uint8_t>(ClampToUnit(srgb) * 255.0f + 0.5f);
    }
}

namespace PluginVideoRecord
{
    void PluginVideoRecordDx12Capture::ConvertPixels(const std::uint8_t* sourcePixels, CapturedFrame& frame) const
    {
        const size_t rowBytes = static_cast<size_t>(captureWidth_) * 4u;
        if (format_ == DXGI_FORMAT_B8G8R8A8_UNORM ||
            format_ == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
            format_ == DXGI_FORMAT_B8G8R8X8_UNORM ||
            format_ == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB)
        {
            for (UINT y = 0; y < captureHeight_; ++y)
            {
                const auto* sourceRow = sourcePixels + static_cast<size_t>(y) * footprint_.Footprint.RowPitch;
                auto* destinationRow = frame.pixels.data() + static_cast<size_t>(y) * rowBytes;
                CopyMemory(destinationRow, sourceRow, rowBytes);
            }

            return;
        }

        for (UINT y = 0; y < captureHeight_; ++y)
        {
            auto* sourceRow = sourcePixels + static_cast<size_t>(y) * footprint_.Footprint.RowPitch;
            auto* destinationRow = frame.pixels.data() + static_cast<size_t>(y) * rowBytes;

            for (UINT x = 0; x < captureWidth_; ++x)
            {
                if (format_ == DXGI_FORMAT_R10G10B10A2_UNORM)
                {
                    const size_t pixelOffset = static_cast<size_t>(x) * 4u;
                    std::uint32_t packedPixel = 0;
                    CopyMemory(&packedPixel, sourceRow + pixelOffset, sizeof(packedPixel));
                    destinationRow[pixelOffset + 0] = ConvertUnorm10ToByte((packedPixel >> 20) & 0x3FFu);
                    destinationRow[pixelOffset + 1] = ConvertUnorm10ToByte((packedPixel >> 10) & 0x3FFu);
                    destinationRow[pixelOffset + 2] = ConvertUnorm10ToByte(packedPixel & 0x3FFu);
                    destinationRow[pixelOffset + 3] = 0xFF;
                }
                else if (format_ == DXGI_FORMAT_R16G16B16A16_FLOAT)
                {
                    const size_t sourcePixelOffset = static_cast<size_t>(x) * 8u;
                    const size_t destinationPixelOffset = static_cast<size_t>(x) * 4u;
                    const auto* halfPixel =
                        reinterpret_cast<const std::uint16_t*>(sourceRow + sourcePixelOffset);
                    destinationRow[destinationPixelOffset + 0] = ConvertFloatToSdrByte(ConvertHalfToFloat(halfPixel[2]));
                    destinationRow[destinationPixelOffset + 1] = ConvertFloatToSdrByte(ConvertHalfToFloat(halfPixel[1]));
                    destinationRow[destinationPixelOffset + 2] = ConvertFloatToSdrByte(ConvertHalfToFloat(halfPixel[0]));
                    destinationRow[destinationPixelOffset + 3] = 0xFF;
                }
                else
                {
                    const size_t pixelOffset = static_cast<size_t>(x) * 4u;
                    destinationRow[pixelOffset + 0] = sourceRow[pixelOffset + 2];
                    destinationRow[pixelOffset + 1] = sourceRow[pixelOffset + 1];
                    destinationRow[pixelOffset + 2] = sourceRow[pixelOffset + 0];
                    destinationRow[pixelOffset + 3] = 0xFF;
                }
            }
        }
    }
}
