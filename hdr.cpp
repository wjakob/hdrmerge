#include "hdrmerge.h"
#include <omp.h>
#include <string.h>

float compute_weight(uint16_t value, uint16_t blacklevel, uint16_t saturation) {
	const float alpha = -1.0f / 10.0f;
	const float beta = 1.0f / std::exp(4.0f*alpha);

	float scaled = (value - blacklevel) / (float) (saturation - blacklevel);

	if (scaled <= 0 || scaled >= 1)
		return 0;

	return beta * std::exp(alpha * (1/scaled + 1/(1-scaled)));
}

void ExposureSeries::merge() {
	merged = new float[width * height];

	/* Precompute some tables for weights and normalized pixel values */
	float weight_tbl[0xFFFF], value_tbl[0xFFFF];
	for (int i=0; i<0xFFFF; ++i) {
		weight_tbl[i] = compute_weight((uint16_t) i, blacklevel, saturation);
		value_tbl[i] = (float) (i - blacklevel) / (float) (whitepoint - blacklevel);
	}

	/* Fast path (only one exposure) */
	if (size() == 1) {
		#pragma omp parallel for
		for (size_t y=0; y<height; ++y) {
			uint16_t *src = exposures[0].image + y * width;
			float *dst = merged + y * width;
			for (size_t x=0; x<width; ++x)
				*dst++ = value_tbl[*src++];
		}
		exposures[0].release();
		return;
	}

	cout << "Merging " << size() << " exposures .." << endl;
	#pragma omp parallel for
	for (size_t y=0; y<height; ++y) {
		uint32_t offset = y * width;
		//offset = 11002202;
		for (size_t x=0; x<width; ++x) {
			float value = 0, total_exposure = 0;

			//cout << "Offset=" << offset << endl;
			for (size_t img=0; img<size(); ++img) {
				uint16_t pxvalue = exposures[img].image[offset];
				float weight = weight_tbl[pxvalue];
				value += value_tbl[pxvalue] * weight;
				total_exposure += exposures[img].exposure * weight;

			//	cout << img << ": " << pxvalue << " => weight=" << weight << ", value=" << value << endl;
			}

			if (total_exposure > 0)
				value /= total_exposure;

			//cout << " ====>> reference = " << value << endl;
			float reference = value;
			value = total_exposure = 0;

			float blacklevel = this->blacklevel, scale = this->whitepoint - blacklevel;

			for (size_t img=0; img<size(); ++img) {
				float predicted = reference * exposures[img].exposure * scale + blacklevel;
				if (predicted <= 0 || predicted >= 65535.0f)
					continue;
				uint16_t predicted_pxvalue = (uint16_t) (predicted + 0.5f);
				uint16_t pxvalue = exposures[img].image[offset];
				float weight = weight_tbl[predicted_pxvalue];

				value += value_tbl[pxvalue] * weight;
				total_exposure += exposures[img].exposure * weight;
///				cout << img << ": " << value_tbl[pxvalue] << " (pred=" << reference * exposures[img].exposure << ")  => weight=" << weight << ", value=" << value << endl;
//				cout << pxvalue << ", " << predicted_pxvalue << ", " << exposures[img].exposure << "; ";
			}
			//cout << endl;

			if (total_exposure > 0)
				value /= total_exposure;
//			cout << " ====>> reference = " << value << endl;

			merged[offset++] = value;
		}
	}
	for (size_t i=0; i<exposures.size(); ++i)
		exposures[i].release();
}


void ExposureSeries::demosaic(float *colormatrix) {
	const int R = 0, G = 1, B = 2;
	const int tsize = 256;

	struct DemosaicBuffer {
		float rgb[2][tsize][tsize][3];
	};

	/* Allocate a big buffer for the interpolated colors */
	float (*output)[3] = (float (*)[3]) calloc(width*height, sizeof(float)*3);

	const float xyz_rgb[3][3] = { /* XYZ from RGB */
        { 0.412453, 0.357580, 0.180423 },
        { 0.212671, 0.715160, 0.072169 },
        { 0.019334, 0.119193, 0.950227 }
    };

	/* Temporary tile storage */
	DemosaicBuffer *buffers = new DemosaicBuffer[omp_get_max_threads()];

	size_t offset = 0;
	for (size_t y=0; y<height; ++y) {
		for (size_t x=0; x<width; ++x) {
			output[offset][fc(x, y)] = merged[offset];
			offset++;
		}
	}

	/* Process the image in tiles */
	std::vector<std::pair<size_t, size_t>> tiles;
	for (size_t top = 2; top < height - 5; top += tsize - 6)
		for (size_t left = 2; left < width - 5; left += tsize - 6)
			tiles.push_back(std::make_pair(left, top));

	//#pragma omp parallel for /* Parallelize over tiles */ ///XXX
	for (size_t tile=0; tile<tiles.size(); ++tile) {
		DemosaicBuffer &buf = buffers[0];//omp_get_thread_id()]; XXX
		size_t left = tiles[tile].first, top = tiles[tile].second;
		memset(buf.rgb, 0, sizeof(float)*3*2*tsize*tsize);//XXX

		for (size_t y=top; y<top+tsize && y<height-2; ++y) {
			/* Interpolate green horizontally and vertically, starting
			   at the first position where it is missing */
			size_t x = left + (fc(left, y) & 1), color = fc(x, y);

			for (; x<left+tsize && x<width-2; x += 2) {
				float (*pix)[3] = output + y*width + x;

				float interp_h = 0.25f * ((pix[-1][G] + pix[0][color] + pix[1][G]) * 2
					  - pix[-2][color] - pix[2][color]);
				float interp_v = 0.25*((pix[-width][G] + pix[0][color] + pix[width][G]) * 2
					  - pix[-2*width][color] - pix[2*width][color]) ;

				/* Don't allow the interpolation to create new local maxima / minima */
				buf.rgb[0][y-top][x-left][G] = clamp(interp_h, pix[-1][G], pix[1][G]);
				buf.rgb[1][y-top][x-left][G] = clamp(interp_v, pix[-width][G], pix[width][G]);
			}
		}

		/* Interpolate red and blue, and convert to CIELab */
		for (int dir=0; dir<2; ++dir) {
			for (size_t y=top+1; y<top+tsize-1 && y<height-3; ++y) {
				for (size_t x = left+1; x<left+tsize-1 && x<width-3; ++x) {
					float (*pix)[3] = output + y*width + x;
					float (*interp)[3] = &buf.rgb[dir][y-top][x-left];

					/* Determine the color at the current pixel */
					int color = fc(x, y);

					if (color == G) {
						color = fc(x, y+1);
						/* Interpolate both red and green */
						float interp = pix[0][G] + (0.5f*(
						    pix[-1][2-color] + pix[1][2-color] - interp[-1][G] - interp[1][G]));
						interp[0][2-color] = std::max(0.0f, interp);

						interp = pix[0][G] + (0.5f*(
							pix[-width][color] + pix[width][color] - interp[-tsize][1] - interp[tsize][1]));
						interp[0][color] = std::max(0.0f, interp);
					} else {
						/* Interpolate the other color */
						color = 2 - color;
						float interp = interp[0][G] + (0.25f * (
								pix[-width-1][color] + pix[-width+1][color]
							  + pix[+width-1][color] + pix[+width+1][color]
							  - interp[-tsize-1][G] - interp[-tsize+1][G]
							  - interp[+tsize-1][G] - interp[+tsize+1][G]));
						interp[0][color] = std::max(0.0f, interp);
					}

					/* Forward the color at the current pixel with out modification */
					color = fc(x, y);
					rix[0][color] = pix[0][color];

					float rgb[3];
					for (int i=0; i<3; ++i)
						for (int j=0; j<3; ++j)
							colormatrix[3*i+j]* rix[0][j]
						}
					}
				}
			}
		}

		writeOpenEXR("test0.exr", tsize, tsize, 3, (float *) buf.rgb[0], metadata, false);
		writeOpenEXR("test1.exr", tsize, tsize, 3, (float *) buf.rgb[1], metadata, false);

		exit(-1);
	}

#if 0
	//writeOpenEXR("test.exr", es.width, es.height, 1, es.image, es.metadata, false);
#endif

	delete[] buffers;
}
