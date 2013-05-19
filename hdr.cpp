#include "hdrmerge.h"
#include <omp.h>
#include <string.h>
#include "Eigen/QR"

float compute_weight(uint16_t value, uint16_t blacklevel, uint16_t saturation) {
	const float alpha = -1.0f / 10.0f;
	const float beta = 1.0f / std::exp(4.0f*alpha);

	float scaled = (value - blacklevel) / (float) (saturation - blacklevel);

	if (scaled <= 0 || scaled >= 1)
		return 0;

	return beta * std::exp(alpha * (1/scaled + 1/(1-scaled)));
}

void ExposureSeries::merge() {
	image_merged = new float[width * height];

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
			float *dst = image_merged + y * width;
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

			image_merged[offset++] = value;
		}
	}
	for (size_t i=0; i<exposures.size(); ++i)
		exposures[i].release();
}


void ExposureSeries::demosaic(float *sensor2xyz) {
	/* This function is based on the AHD code from dcraw, which in turn
       builds on work by Keigo Hirakawa, Thomas Parks, and Paul Lee. */
	const int G = 1, tsize = 256;

	struct DemosaicBuffer {
		/* Horizontally and vertically interpolated sensor colors */
		float3 rgb[2][tsize][tsize];

		/* CIElab color values */
		float3 cielab[2][tsize][tsize];

		/* Homogeneity map */
		uint8_t homo[2][tsize][tsize];
	};

	/* Temporary tile storage */
	DemosaicBuffer *buffers = new DemosaicBuffer[omp_get_max_threads()];

	cout << "AHD demosaicing .." << endl;

	/* Allocate a big buffer for the interpolated colors */
	image_demosaiced = new float3[width*height];

	size_t offset = 0;
	float maxvalue = 0;
	for (size_t y=0; y<height; ++y) {
		for (size_t x=0; x<width; ++x) {
			float value = image_merged[offset];
			image_demosaiced[offset][fc(x, y)] = value;
			if (value > maxvalue)
				maxvalue = value;
			offset++;
		}
	}

	/* The AHD implementation below doesn't interpolate colors on a 5-pixel wide
       boundary region -> use a naive averaging method on this region instead. */
	const size_t border = 5;
	for (size_t y=0; y<height; ++y) {
		for (size_t x=0; x<width; ++x) {
			if (x == border && y >= border && y < height-border)
				x = width-border; /* Jump over the center part of the image */

			float binval[3] = {0, 0, 0};
			int bincount[3] = {0, 0, 0};

			for (size_t ys=y-1; ys != y+2; ++ys) {
				for (size_t xs=x-1; xs != x+2; ++xs) {
					if (ys < height && xs < width) {
						int col = fc(xs, ys);
						binval[col] += image_demosaiced[ys*width+xs][col];
						++bincount[col];
					}
				}
			}

			int col = fc(x, y);
			for (int c=0; c<3; ++c) {
				if (col != c)
					image_demosaiced[y*width+x][c] = bincount[c] ? (binval[c]/bincount[c]) : 1.0f;
			}
		}
	}

	/* Matrix that goes from sensor to normalized XYZ tristimulus values */
	float sensor2xyz_n[3][3], sensor2xyz_n_maxvalue = 0;
	const float d65_white[3] = { 0.950456, 1, 1.088754 };
	for (int i=0; i<3; ++i) {
		for (int j=0; j<3; ++j) {
			sensor2xyz_n[i][j] = sensor2xyz[i*3+j] / d65_white[i];
			sensor2xyz_n_maxvalue = std::max(sensor2xyz_n_maxvalue, sensor2xyz_n[i][j]);
		}
	}

	/* Scale factor that is guaranteed to push XYZ values into the range [0, 1] */
	float scale = 1.0 / (maxvalue * sensor2xyz_n_maxvalue);

	/* Precompute a table for the nonlinear part of the CIELab conversion */
	const int cielab_table_size = 0xFFFF;
	float cielab_table[cielab_table_size];
	for (int i=0; i<cielab_table_size; ++i) {
		float r = i * 1.0f / (cielab_table_size-1);
		cielab_table[i] = r > 0.008856 ? std::pow(r, 1.0f / 3.0f) : 7.787f*r + 4.0f/29.0f;
	}

	/* Process the image in tiles */
	std::vector<std::pair<size_t, size_t>> tiles;
	for (size_t top = 2; top < height - 5; top += tsize - 6)
		for (size_t left = 2; left < width - 5; left += tsize - 6)
			tiles.push_back(std::make_pair(left, top));

	#pragma omp parallel for /* Parallelize over tiles */
	for (size_t tile=0; tile<tiles.size(); ++tile) {
		DemosaicBuffer &buf = buffers[omp_get_thread_num()];
		size_t left = tiles[tile].first, top = tiles[tile].second;

		for (size_t y=top; y<top+tsize && y<height-2; ++y) {
			/* Interpolate green horizontally and vertically, starting
			   at the first position where it is missing */
			size_t x = left + (fc(left, y) & 1), color = fc(x, y);

			for (; x<left+tsize && x<width-2; x += 2) {
				float3 *pix = image_demosaiced + y*width + x;

				float interp_h = 0.25f * ((pix[-1][G] + pix[0][color] + pix[1][G]) * 2
					  - pix[-2][color] - pix[2][color]);
				float interp_v = 0.25*((pix[-width][G] + pix[0][color] + pix[width][G]) * 2
					  - pix[-2*width][color] - pix[2*width][color]);

				/* Don't allow the interpolation to create new local maxima / minima */
				buf.rgb[0][y-top][x-left][G] = clamp(interp_h, pix[-1][G], pix[1][G]);
				buf.rgb[1][y-top][x-left][G] = clamp(interp_v, pix[-width][G], pix[width][G]);
			}
		}

		/* Interpolate red and blue, and convert to CIELab */
		for (int dir=0; dir<2; ++dir) {
			for (size_t y=top+1; y<top+tsize-1 && y<height-3; ++y) {
				for (size_t x = left+1; x<left+tsize-1 && x<width-3; ++x) {
					float3 *pix = image_demosaiced + y*width + x;
					float3 *interp = &buf.rgb[dir][y-top][x-left];
					float3 *lab = &buf.cielab[dir][y-top][x-left];

					/* Determine the color at the current pixel */
					int color = fc(x, y);

					if (color == G) {
						color = fc(x, y+1);
						/* Interpolate both red and green */
						interp[0][2-color] = std::max(0.0f, pix[0][G] + (0.5f*(
						    pix[-1][2-color] + pix[1][2-color] - interp[-1][G] - interp[1][G])));

						interp[0][color] = std::max(0.0f,  pix[0][G] + (0.5f*(
							pix[-width][color] + pix[width][color] - interp[-tsize][1] - interp[tsize][1])));
					} else {
						/* Interpolate the other color */
						color = 2 - color;
						interp[0][color] = std::max(0.0f, interp[0][G] + (0.25f * (
								pix[-width-1][color] + pix[-width+1][color]
							  + pix[+width-1][color] + pix[+width+1][color]
							  - interp[-tsize-1][G] - interp[-tsize+1][G]
							  - interp[+tsize-1][G] - interp[+tsize+1][G])));
					}

					/* Forward the color at the current pixel with out modification */
					color = fc(x, y);
					interp[0][color] = pix[0][color];

					/* Convert to CIElab */
					float xyz[3] = { 0, 0, 0 };
					for (int i=0; i<3; ++i)
						for (int j=0; j<3; ++j)
							xyz[i] += sensor2xyz_n[i][j] * interp[0][j];

					for (int i=0; i<3; ++i)
						xyz[i] = cielab_table[std::max(0, std::min(cielab_table_size-1,
								(int) (xyz[i] * scale * cielab_table_size)))];

					lab[0][0] = (116.0f * xyz[1] - 16);
					lab[0][1] = 500.0f * (xyz[0] - xyz[1]);
					lab[0][2] = 200.0f * (xyz[1] - xyz[2]);
				}
			}
		}

		/*  Build homogeneity maps from the CIELab images: */
		const int offset_table[4] = { -1, 1, -tsize, tsize };
		memset(buf.homo, 0, 2*tsize*tsize);
		for (size_t y=top+2; y < top+tsize-2 && y < height-4; ++y) {
			for (size_t x=left+2; x< left+tsize-2 && x < width-4; ++x) {
				float ldiff[2][4], abdiff[2][4];

				for (int dir=0; dir < 2; dir++) {
					float3 *lab = &buf.cielab[dir][y-top][x-left];

					for (int i=0; i < 4; i++) {
						int offset = offset_table[i];

						/* Luminance and chromaticity differences in 4 directions,
						   for each of the two interpolated images */
						ldiff[dir][i] = std::abs(lab[0][0] - lab[offset][0]);
						abdiff[dir][i] = square(lab[0][1] - lab[offset][1])
						   + square(lab[0][2] - lab[offset][2]);
					}
				}

				float leps  = std::min(std::max(ldiff[0][0], ldiff[0][1]),
				                       std::max(ldiff[1][2], ldiff[1][3]));
				float abeps = std::min(std::max(abdiff[0][0], abdiff[0][1]),
				                       std::max(abdiff[1][2], abdiff[1][3]));

				/* Count the number directions in which the above thresholds can
				   be maintained, for each of the two interpolated images */
				for (int dir=0; dir < 2; dir++)
					for (int i=0; i < 4; i++)
						if (ldiff[dir][i] <= leps && abdiff[dir][i] <= abeps)
							buf.homo[dir][y-top][x-left]++;
			}
		}

		/*  Combine the most homogenous pixels for the final result */
		for (size_t y=top+3; y < top+tsize-3 && y < height-5; ++y) {
			for (size_t x=left+3; x < left+tsize-3 && x < width-5; ++x) {
				/* Look, which of the to images is more homogeneous in a 3x3 neighborhood */
				int hm[2] = {0, 0};
				for (int dir=0; dir < 2; dir++)
					for (size_t i=y-top-1; i <= y-top+1; i++)
						for (size_t j=x-left-1; j <= x-left+1; j++)
							hm[dir] += buf.homo[dir][i][j];

				if (hm[0] != hm[1]) {
					/* One of the images was more homogeneous */
					for (int col=0; col<3; ++col)
						image_demosaiced[y*width+x][col] = buf.rgb[hm[1] > hm[0] ? 1 : 0][y-top][x-left][col];
				} else {
					/* No clear winner, blend */
					for (int col=0; col<3; ++col)
						image_demosaiced[y*width+x][col] = 0.5f*(buf.rgb[0][y-top][x-left][col]
							+ buf.rgb[1][y-top][x-left][col]);
				}
			}
		}
	}

	delete[] buffers;
	delete[] image_merged;
	image_merged = NULL;
}

void ExposureSeries::transform_color(float *sensor2xyz, bool xyz) {
	const float xyz2rgb[3][3] = {
		{ 3.240479f, -1.537150f, -0.498535f },
		{-0.969256f, +1.875991f, +0.041556f },
		{ 0.055648f, -0.204043f, +1.057311f }
	};
	float M[3][3];

	if (xyz) {
		cout << "Transforming to XYZ color space .." << endl;
	} else {
		cout << "Transforming to sRGB color space .." << endl;
	}

	for (int i=0; i<3; ++i) {
		for (int j=0; j<3; ++j) {
			if (xyz) {
				M[i][j] = sensor2xyz[3*i+j];
			} else {
				float accum = 0;
				for (int k=0; k<3; ++k)
					accum += xyz2rgb[i][k] * sensor2xyz[3*k+j];
				M[i][j] = accum;
			}
		}
	}

	#pragma omp parallel for
	for (size_t y=0; y<height; ++y) {
		float3 *ptr = image_demosaiced + y*width;
		for (size_t x=0; x<width; ++x) {
			float accum[3] = {0, 0, 0};
			for (int i=0; i<3; ++i)
				for (int j=0; j<3; ++j)
					accum[i] += M[i][j] * ptr[0][j];
			for (int i=0; i<3; ++i)
				ptr[0][i] = accum[i];
			++ptr;
		}
	}
}

void ExposureSeries::scale(float factor) {
	cout << "Scaling the image by a factor of " << factor << " .." << endl;

	if (image_merged) {
		#pragma omp parallel for
		for (size_t y=0; y<height; ++y) {
			float *ptr = image_merged + y*width;
			for (size_t x=0; x<width; ++x)
				*ptr++ *= factor;
		}
	}

	if (image_demosaiced) {
		#pragma omp parallel for
		for (size_t y=0; y<height; ++y) {
			float3 *ptr = image_demosaiced + y*width;
			for (size_t x=0; x<width; ++x) {
				for (int i=0; i<3; ++i)
					ptr[0][i] *= factor;
				ptr++;
			}
		}
	}
}

void ExposureSeries::crop(int offs_x, int offs_y, int w, int h) {
	cout << "Cropping to " << w << "x" << h << " .." << endl;
	if (offs_x < 0 || offs_y < 0 || w <= 0 || h <= 0 || offs_x+w > (int) width || offs_y+h > (int) height)
		throw std::runtime_error("crop(): selected an invalid rectangle!");

	if (image_merged) {
		float *temp = new float[w*h];

		for (int y=0; y<h; ++y) {
			float *dst = temp + w * y;
			float *src = image_merged + width * (y+offs_y) + offs_x;

			for (int x=0; x<w; ++x)
				*dst++ = *src++;
		}
		delete[] image_merged;
		image_merged = temp;
	}

	if (image_demosaiced) {
		float3 *temp = new float3[w*h];

		for (int y=0; y<h; ++y) {
			float3 *dst = temp + w * y;
			float3 *src = image_demosaiced + width * (y+offs_y) + offs_x;

			for (int x=0; x<w; ++x) {
				for (int c=0; c<3; ++c)
					(*dst)[c] = (*src)[c];
				++dst;
				++src;
			}
		}
		delete[] image_demosaiced;
		image_demosaiced = temp;
	}

	width = w;
	height = h;
}

void ExposureSeries::whitebalance(int offs_x, int offs_y, int w, int h) {
	if (offs_x < 0 || offs_y < 0 || w <= 0 || h <= 0 || offs_x+w > (int) width || offs_y+h > (int) height)
		throw std::runtime_error("crop(): selected an invalid rectangle!");

	float scale[3] = { 0, 0, 0 };
	for (int y=0; y<h; ++y) {
		float3 *ptr = image_demosaiced + (offs_y+y) * width + offs_x;
		for (int x=0; x<w; ++x) {
			for (int c=0; c<3; ++c)
				scale[c] += (*ptr)[c];
			++ptr;
		}
	}

	for (int c=0; c<3; ++c)
		scale[c] = 1.0f / scale[c];

	float normalization = 3.0f / (scale[0] + scale[1] + scale[2]);

	for (int c=0; c<3; ++c)
		scale[c] = normalization * scale[c];

	whitebalance(scale);
}

void ExposureSeries::whitebalance(float *scale) {
	cout << "Applying white balance (multipliers = " << scale[0] << ", " << scale[1] << ", " << scale[2] << ")" << endl;
	for (size_t y=0; y<height; ++y) {
		float3 *ptr = image_demosaiced + y*width;
		for (size_t x=0; x<width; ++x) {
			for (int c=0; c<3; ++c)
				(*ptr)[c] *= scale[c];
			ptr++;
		}
	}
}

void ExposureSeries::vcal() {
	/* Simplistic vignetting correction -- assumes that vignetting is radially symmetric
	   around the image center and least-squares-fits a 6-th order polynomial. Probably
	   good enough for most purposes though.. */
	double center_x = width / 2.0, center_y = height / 2.0;
	size_t skip = 10, nPixels = ((width+skip-1)/skip) * ((height+skip-1)/skip);
	double size_scale = 1.0 / std::max(width, height);

	Eigen::MatrixXd A(nPixels, 4);
	Eigen::VectorXd b(nPixels);

	cout << "Fitting a 6-th order polynomial to the vignetting profile .." << endl;
	size_t idx = 0;

	for (size_t y=0; y<height; y += skip) {
		float3 *ptr = image_demosaiced + y*width;
		double dy = ((y + 0.5f) - center_y)*size_scale, dy2 = dy*dy;
		for (size_t x=0; x<width; x += skip) {
			double luminance = ptr[0][0] * 0.212671 + ptr[0][1] * 0.715160 + ptr[0][2] * 0.072169;
			double dx = ((x + 0.5f) - center_x) * size_scale, dx2 = dx*dx;
			double dist2 = dx2+dy2, dist4 = dist2*dist2, dist6 = dist4*dist2;
			A(idx, 0) = 1.0f;
			A(idx, 1) = dist2;
			A(idx, 2) = dist4;
			A(idx, 3) = dist6;
			b(idx) = luminance;
			ptr += skip;
			idx++;
		}
	}
	if (nPixels != idx) {
		cout << idx << " vs " << nPixels << endl;
		exit(-1);
	}

	Eigen::VectorXd result = A.colPivHouseholderQr().solve(b);
	result /= result(0);

	cout << "Done. Pass --vcorr \"" << result[1] << ", " << result[2] << ", " << result[3]
		 << "\" to hdrmerge in future runs (or add to 'hdrmerge.cfg')" << endl;

	vcorr((float) result[1], (float) result[2], (float) result[3]);
}

void ExposureSeries::vcorr(float a, float b, float c) {
	double center_x = width / 2.0, center_y = height / 2.0;
	double size_scale = 1.0 / std::max(width, height);

	cout << "Correcting for vignetting .." << endl;

	#pragma omp parallel for
	for (size_t y=0; y<height; ++y) {
		float3 *ptr = image_demosaiced + y*width;
		double dy = ((y + 0.5f) - center_y)*size_scale, dy2 = dy*dy;
		for (size_t x=0; x<width; ++x) {
			double dx = ((x + 0.5f) - center_x)*size_scale, dx2 = dx*dx;
			double dist2 = dx2+dy2, dist4 = dist2*dist2, dist6 = dist4*dist2;
			float corr = 1.0f / (1.0f + dist2*a + dist4*b + dist6*c);
			for (int c=0; c<3; ++c)
				ptr[0][c] *= corr;
			++ptr;
		}
	}
}
