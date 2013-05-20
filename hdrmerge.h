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

	/* Black level and whitepoint as determined by RawSpeed */
	int blacklevel, whitepoint;

	/* Merged high dynamic range image (no demosaicing yet) */
	float *image_merged;

	/* Merged and demosaiced image */
	float3 *image_demosaiced;

	/* dcraw-style color filter array description */
	int filter;

	/* Saturation threshold */
	float saturation;

	/* Tables for transforming from sensor values to exposures / weights */
	float weight_tbl[0xFFFF], value_tbl[0xFFFF];

	inline ExposureSeries() : 
		image_merged(NULL), image_demosaiced(NULL) { }

	~ExposureSeries() {
		if (image_merged)
			delete[] image_merged;
		if (image_demosaiced)
			delete[] image_demosaiced;
	}

	/// Return the color at position (x, y)
	inline int fc(int x, int y) const {
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

	/// Initialize the exposure / weight table
	void initTables(float saturation);

	/// Merge all exposures into a single HDR image and release the RAW data
	void merge();

	/// Estimate the exposure times in case the EXIF tags can't be trusted
	void fitExposureTimes();

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

	/// Remove vignetting / calibration routine
	void vcal();

	/// Correct for vignetting using a radial polynomial 1+ax^2+bx^4+cx^6
	void vcorr(float a, float b, float c);

	/// Return the number of exposures
	inline size_t size() const {
		return exposures.size();
	}

	/// Evaluate a pixel in one of the images
	float eval(int img, int x, int y) const {
		return value_tbl[exposures[img].image[x + y*width]];
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
