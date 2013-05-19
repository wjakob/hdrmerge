#if !defined(__HDRMERGE_H)
#define __HDRMERGE_H

#include <string>
#include <vector>
#include <map>
#include <stdexcept>
#include <cmath>
#include <iostream>
#include <stdint.h>
#include <memory>

using std::cout;
using std::cerr;
using std::endl;

/// String map for metadata
typedef std::map<std::string, std::string> StringMap;

/// Rgb color type
typedef float float3[3];

/// Abstract reconstruction filter
class ReconstructionFilter {
public:
	virtual float getRadius() const = 0;
	virtual float eval(float x) const = 0;
};

/// Records a single RAW exposure
struct Exposure {
	std::string filename;
	float exposure;
	float shown_exposure;
	uint16_t *image;

	inline Exposure(const std::string &filename)
	 : filename(filename), exposure(-1), image(NULL) { }

	inline ~Exposure() {
		release();
	}

	inline void release() {
		if (image) {
			delete[] image;
			image = NULL;
		}
	}

	/// Return the exposure has a human-readable string
	std::string toString() const {
		char buf[10];
		if (exposure < 1)
			snprintf(buf, sizeof(buf), "1/%.4g", 1/exposure);
		else
			snprintf(buf, sizeof(buf), "%.4g", exposure);
		return buf;
	}
};

/// Stores a series of exposures, manages demosaicing and subsequent steps
struct ExposureSeries {
	std::vector<Exposure> exposures;
	StringMap metadata;

	/* Width and height of the cropped RAW images */
	size_t width, height;

	/* Black level, saturation value and whitepoint (max theoretical range) */
	int blacklevel, saturation, whitepoint;

	/* Merged high dynamic range image (no demosaicing yet) */
	float *image_merged;

	/* Merged and demosaiced image */
	float3 *image_demosaiced;

	/* dcraw-style color filter array description */
	int filter;

	inline ExposureSeries() : saturation(0),
		image_merged(NULL), image_demosaiced(NULL) { }

	~ExposureSeries() {
		if (image_merged)
			delete[] image_merged;
		if (image_demosaiced)
			delete[] image_demosaiced;
	}

	/// Return the color at position (x, y)
	inline int fc(int x, int y) {
		return (filter >> (((y << 1 & 14) + (x & 1)) << 1) & 3);
	}

	/**
	 * Add a file to the exposure series (or, optionally, a sequence
	 * such as file_%03i.png expressed using the printf-style format)
	 */
	void add(const std::string &filename);

	/**
	 * Check that all exposures are valid, and that they satisfy
	 * a few basic requirements such as:
	 *  - all images use the same ISO speed and aperture setting
	 *  - the images were taken using manual focus and manual exposure mode
     *  - there are no duplicate exposures.
	 *
	 * This also sorts the exposures in case they weren't ordered already
	 */
	void check();

	/**
	 * Run dcraw on an entire exposure series (in parallel)
	 * and fill the exposure series with a normalized RGB floating
	 * point image representation
	 */
	void load();

	/// Merge all exposures into a single HDR image and release the RAW data
	void merge();

	/// Perform demosaicing
	void demosaic(float *sensor2xyz);

	/// Transform the image into the right color space
	void transform_color(float *sensor2xyz, bool xyz);

	/// Scale the image brightness by a given factor
	void scale(float factor);

	/// Resample the image to a different resolution
	void resample(const ReconstructionFilter &filter, size_t w, size_t h);

	/// Crop a rectangular region
	void crop(int x, int y, int w, int h);

	/// Apply white balancing
	void whitebalance(float *scale);

	/// Apply white balancing based on a grey patch
	void whitebalance(int xoffs, int yoffs, int w, int h);

	/// Return the number of exposures
	inline size_t size() const {
		return exposures.size();
	}
};

/// Return the number of processors available for multithreading
extern int getProcessorCount();

/**
 * Write a lossless floating point OpenEXR file using either half or
 * single precision (grayscale or RGB)
 */
extern void writeOpenEXR(const std::string &filename, size_t w, size_t h,
	int channels, float *data, const StringMap &metadata, bool writeHalf);

/// Generate a uniformly distributed random number in [0, 1)
inline float randf() {
	#define RS_SCALE (1.0f / (1.0f + RAND_MAX))
    float f;
    do {
       f = (((rand () * RS_SCALE) + rand ()) * RS_SCALE + rand()) * RS_SCALE;
    } while (f >= 1); /* Round off */

    return f;
}

inline float clamp(float value, float min, float max) {
	if (min > max)
		std::swap(min, max);
	return std::min(std::max(value, min), max);
}

inline float square(float value) {
	return value*value;
}

#endif /* __HDRMERGE_H */
