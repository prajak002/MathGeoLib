#include "MiniFloat.h"

float Float16ToFloat32(uint16_t float16)
{
	uint32_t sign = (float16 & 0x8000) << 16;
	uint32_t exponent = float16 & 0x7C00;
	uint32_t mantissa = float16 & 0x03FF;

	// Shift back the decoded mantissa to proper position.
	mantissa <<= 13;

	// Reconstruct the float exponent.
	if (exponent == 0x7C00) // If the read exponent was all ones, reconstruct 11111111.
		exponent = 0xFF;
	else if (exponent != 0) // If the read exponent was not zero, it was a normal number.
		exponent = (exponent >> 10) - 15 + 127;
	// else exponent == 0, meaning a zero or a denormal.

	uint32_t value = sign | (exponent << 23) | mantissa;

	return *(float*)&value;
}

uint16_t Float32ToFloat16(float float32)
{
	// Float structure:
	// 1-bit sign
	// 8-bit exponent
	// 23-bit mantissa
	// s eeeeeeee mmmmmmmmmmmmmmmmmmmmmmm
	// Different float categories:
	// 0 00000000 00000000000000000000000  +zero
	// 1 00000000 00000000000000000000000  -zero
	// s 00000000 mmmmmmmmmmmmmmmmmmmmmmm  A denormal number, mmmmm != 0, interpreted as (-1)^s * 2^-126 * 0.mmmmm
	// s eeeeeeee xxxxxxxxxxxxxxxxxxxxxxx  A normal number, eeeee != 0, interpreted as (-1)^s * 2^(e-127) * 1.mmmmm
	// 0 11111111 00000000000000000000000  +inf
	// 1 11111111 00000000000000000000000  -inf
	// y 11111111 1xxxxxxxxxxxxxxxxxxxxxx  Quiet NaN, y and xxxxx are arbitrary (custom) payload for the NaN.
	// y 11111111 0xxxxxxxxxxxxxxxxxxxxxx  Signalling NaN, y and xxxxx != 0 is arbitrary payload for the NaN.

	// When writing our custom low-precision minifloat, make sure that values in each of the above categories stays in
	// the same category, if possible:
	// +zero: Reducing bits does not affect the value.
	// -zero: Reducing bits does not affect the value. If sending unsigned, -zero becomes +zero.
	// denormals: Reducing bits from mantissa gracefully loses precision. Reducing exponent does not affect the value.
	//            If sending unsigned, negative denormals flush to zero.
	// normals: Reducing bits from exponent can cause the exponent to overflow or underflow.
	//          If the exponent is too large to be encoded, +inf/-inf is sent instead.
	//          If the exponent is too small to be encoded, the value is flushed to zero. \todo Could create a denormal!
	// +inf: Reducing bits does not affect the value.
	// -inf: Reducing bits from exponent or mantissa does not matter. If sending unsigned, -inf flushes to zero.
	// QNaN/SNaN: Reducing bits loses data from the custom NaN payload field. If mantissaBits == 0, cannot differentiat
	//            between QNaN and SNaN.

	uint32_t v = *(uint32_t*)&float32;
	uint32_t biasedExponent = (v & 0x7F800000) >> 23;
	uint32_t mantissa = v & 0x7FFFFF;
	uint32_t sign = (v & 0x80000000) >> 16; // If true, the float is negative.

	// The maximum biased exponent value in the reduced precision representation. This corresponds to NaNs and +/-Infs.
	const uint32_t maxBiasedExponent = (1 << 5) - 1;

	int trueExponent = biasedExponent - 127; // The true exponent of the float, if this number is a normal number.
	int newBiasedExponent;

	// Compute the new biased exponent value to send.
	if (biasedExponent != 0xFF && biasedExponent != 0) // Is this a normalized float?
	{
		newBiasedExponent = trueExponent + 15;
		
		// Check if the new biased exponent is too large to be represented, and the float overflows to a +/-Inf.
		if (newBiasedExponent >= (int)maxBiasedExponent)
		{
			newBiasedExponent = maxBiasedExponent;
			mantissa = 0; // To specify that this is an Inf and not a NaN.
		}
		// Check if the new biased exponent underflowed. In that case flush to zero.
		///\todo This is not absolutely correct with respect to denormalized numbers. Underflowing
		/// the exponent should produce a denormalized number, but this directly makes it zero.
		if (newBiasedExponent <= 0)
			newBiasedExponent = mantissa = 0;
	}
	else
		newBiasedExponent = biasedExponent; // either all zeroes (+/-zero or denormal) or all ones (nan or inf).

	// Scrap the given number of precision from the mantissa.
	uint32_t newMantissa = mantissa >> (23 - 10);

	// If the float was a SNaN, make sure it stays a SNaN after some of the NaN payload was removed.
	if (biasedExponent == 0xFF && mantissa != 0 && newMantissa == 0)
		newMantissa = 1; // Set the mantissa to nonzero to denote a NaN (and don't set the MSB of mantissa, to treat it as SNaN)

	return (uint16_t)(sign | (newBiasedExponent << 10) | newMantissa);
}