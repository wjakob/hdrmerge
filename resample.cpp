#include "hdrmerge.h"
#include <assert.h>

/**
 * Utility class for efficiently resampling discrete 
 * datasets to different resolutions
 *
 * (simplified ported version from Mitsuba)
 */
struct Resampler {
	/**
	 * \brief Create a new Resampler object that transforms between the specified resolutions
	 *
	 * This constructor precomputes all information needed to efficiently perform the
	 * desired resampling operation. For that reason, it is most efficient if it can
	 * be used over and over again (e.g. to resample the equal-sized rows of a bitmap)
	 *
	 * \param sourceRes
	 *      Source resolution
	 * \param targetRes
	 *      Desired target resolution
	 */
	Resampler(const ReconstructionFilter &rfilter, 
			int sourceRes, int targetRes) : m_sourceRes(sourceRes), m_targetRes(targetRes) {
		assert(sourceRes > 0 && targetRes > 0);
		float filterRadius = rfilter.getRadius(), scale = 1.0f, invScale = 1.0f;

		/* Low-pass filter: scale reconstruction filters when downsampling */
		if (targetRes < sourceRes) {
			scale = (float) sourceRes / (float) targetRes;
		    invScale = 1 / scale;
			filterRadius *= scale;
		}

		m_taps = (int) std::floor(filterRadius * 2);
		m_start = new int[targetRes];
		m_weights = new float[m_taps * targetRes];
		m_fastStart = 0;
		m_fastEnd = m_targetRes;

		for (int i=0; i<targetRes; i++) {
			/* Compute the fractional coordinates of the new sample i in the original coordinates */
			float center = (i + (float) 0.5f) / targetRes * sourceRes;

			/* Determine the index of the first original sample that might contribute */
			m_start[i] = (int) std::floor(center - filterRadius + (float) 0.5f);

			/* Determine the size of center region, on which to run fast non condition-aware code */
			if (m_start[i] < 0)
				m_fastStart = std::max(m_fastStart, i + 1);
			else if (m_start[i] + m_taps - 1 >= m_sourceRes)
				m_fastEnd = std::min(m_fastEnd, i - 1);

			float sum = 0;
			for (int j=0; j<m_taps; j++) {
				/* Compute the the position where the filter should be evaluated */
				float pos = m_start[i] + j + (float) 0.5f - center;

				/* Perform the evaluation and record the weight */
				float weight = rfilter.eval(pos * invScale);
				m_weights[i * m_taps + j] = weight;
				sum += weight;
			}

			/* Normalize the contribution of each sample */
			float normalization = 1.0f / sum;
			for (int j=0; j<m_taps; j++) {
				float &value = m_weights[i * m_taps + j];
				value = (float) value * normalization;
			}
		}
		m_fastStart = std::min(m_fastStart, m_fastEnd);
	}

	/// Release all memory
	~Resampler() {
		delete[] m_start;
		delete[] m_weights;
	}

	/**
	 * \brief Resample a multi-channel array
	 *
	 * \param source
	 *     Source array of samples
	 * \param target
	 *     Target array of samples
	 * \param sourceStride
	 *     Stride of samples in the source array. A value
	 *     of '1' implies that they are densely packed.
	 * \param targetStride
	 *     Stride of samples in the source array. A value
	 *     of '1' implies that they are densely packed.
	 * \param channels
	 *     Number of channels to be resampled
	 */
	void resample(const float *source, size_t sourceStride,
			float *target, size_t targetStride, int channels) {
		const int taps = m_taps;
		targetStride = channels * (targetStride - 1);
		sourceStride *= channels;

		/* Resample the left border region, while accounting for the boundary conditions */
		for (int i=0; i<m_fastStart; ++i) {
			int start = m_start[i];

			for (int ch=0; ch<channels; ++ch) {
				float result = 0;
				for (int j=0; j<taps; ++j)
					result += lookup(source, start + j, sourceStride, ch) * m_weights[i * taps + j];
				*target++ = result;
			}

			target += targetStride;
		}

		/* Use a faster, vectorizable loop for resampling the main portion */
		for (int i=m_fastStart; i<m_fastEnd; ++i) {
			int start = m_start[i];

			for (int ch=0; ch<channels; ++ch) {
				float result = 0;
				for (int j=0; j<taps; ++j)
					result += source[sourceStride * (start + j) + ch] * m_weights[i * taps + j];
				*target++ = result;
			}

			target += targetStride;
		}

		/* Resample the right border region, while accounting for the boundary conditions */
		for (int i=m_fastEnd; i<m_targetRes; ++i) {
			int start = m_start[i];

			for (int ch=0; ch<channels; ++ch) {
				float result = 0;
				for (int j=0; j<taps; ++j)
					result += lookup(source, start + j, sourceStride, ch) * m_weights[i * taps + j];
				*target++ = result;
			}

			target += targetStride;
		}
	}
private:
	inline float lookup(const float *source, int pos, size_t stride, int offset) const {
		pos = std::min(std::max(pos, 0), m_sourceRes - 1);
		return source[stride * pos + offset];
	}

private:
	int m_sourceRes;
	int m_targetRes;
	int *m_start;
	float *m_weights;
	int m_fastStart, m_fastEnd;
	int m_taps;
};


void ExposureSeries::resample(const ReconstructionFilter &rfilter, size_t width_t, size_t height_t) {
	cout << "Resampling to " << width_t << "x" << height_t << " .." << endl;
	assert(width_t > 0 && height_t > 0);

	if (width != width_t) {
		/* Re-sample along the X direction */
		Resampler r(rfilter, width, width_t);

		float3 *temp = new float3[width_t * height];

		#pragma omp parallel for
		for (size_t y=0; y<height; ++y) {
			const float3 *srcPtr = image_demosaiced + y * width;
			const float3 *trgPtr = temp + y * width_t;
			r.resample((float *) srcPtr, 1, (float *) trgPtr, 1, 3);
		}

		delete[] image_demosaiced;
		image_demosaiced = temp;
		width = width_t;
	}

	if (height != height_t) {
		/* Re-sample along the Y direction */
		Resampler r(rfilter, height, height_t);

		float3 *temp = new float3[width_t * height_t];

		#pragma omp parallel for
		for (size_t x=0; x<width; ++x) {
			const float3 *srcPtr = image_demosaiced + x;
			const float3 *trgPtr = temp + x;

			r.resample((float *) srcPtr, width, (float *) trgPtr, width_t, 3);
		}

		delete[] image_demosaiced;
		image_demosaiced = temp;
		height = height_t;
	}
}

