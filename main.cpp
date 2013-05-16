#include "hdrmerge.h"

float compute_weight(uint16_t value, uint16_t blacklevel, uint16_t saturation) {
	const float alpha = -1.0f / 10.0f;
	const float beta = 1.0f / std::exp(4.0f*alpha);

	float scaled = (value - blacklevel) / (float) (saturation - blacklevel);

	if (scaled <= 0 || scaled >= 1)
		return 0;

	return beta * std::exp(alpha * (1/scaled + 1/(1-scaled)));
}

float compute_weight(float scaled) {
	const float alpha = -1.0f / 10.0f;
	const float beta = 1.0f / std::exp(4.0f*alpha);

	if (scaled <= 0 || scaled >= 1)
		return 0;

	return beta * std::exp(alpha * (1/scaled + 1/(1-scaled)));
}

void merge(ExposureSeries &es) {
	cout << "Merging " << es.size() << " exposures .." << endl;
	es.image = new float[es.width * es.height];

	/* Precompute some tables for weights and normalized pixel values */
	float weight_tbl[0xFFFF], value_tbl[0xFFFF];
	for (int i=0; i<0xFFFF; ++i) {
		weight_tbl[i] = compute_weight((uint16_t) i, es.blacklevel, es.saturation);
		value_tbl[i] = (float) (i - es.blacklevel) / (float) (es.whitepoint - es.blacklevel);
	}


//	#pragma omp parallel for schedule(dynamic, 1)
	for (int y=0; y<es.height; ++y) {
		uint32_t offset = y * es.width;
		offset = 11002202;
		for (int x=0; x<es.width; ++x) {
			float value = 0, total_exposure = 0;

			cout << "Offset=" << offset << endl;
			for (int img=0; img<es.size(); ++img) {
				uint16_t pxvalue = es.exposures[img].image[offset];
				float weight = weight_tbl[pxvalue];
				value += value_tbl[pxvalue] * weight;
				total_exposure += es.exposures[img].exposure * weight;

				cout << img << ": " << pxvalue << " => weight=" << weight << ", value=" << value << endl;
			}

			if (total_exposure > 0)
				value /= total_exposure;

			cout << " ====>> reference = " << value << endl;
			float reference = value;
			value = total_exposure = 0;

			float blacklevel = es.blacklevel, scale = es.whitepoint - es.blacklevel;

			for (int img=0; img<es.size(); ++img) {
				float predicted = reference * es.exposures[img].exposure * scale + blacklevel;
				if (predicted <= 0 || predicted >= 65535.0f)
					continue;
				uint16_t predicted_pxvalue = (uint16_t) (predicted + 0.5f);
				uint16_t pxvalue = es.exposures[img].image[offset];
				float weight = weight_tbl[predicted_pxvalue];
				
				value += value_tbl[pxvalue] * weight;
				total_exposure += es.exposures[img].exposure * weight;
///				cout << img << ": " << value_tbl[pxvalue] << " (pred=" << reference * es.exposures[img].exposure << ")  => weight=" << weight << ", value=" << value << endl;
				cout << pxvalue << ", " << predicted_pxvalue << ", " << es.exposures[img].exposure << "; ";
			}
			cout << endl;

			if (total_exposure > 0)
				value /= total_exposure;
			cout << " ====>> reference = " << value << endl;

			es.image[offset++] = value;
		}
	}
}

int main(int argc, char **argv) {
	ExposureSeries es;
//	es.add("test-%02i.cr2");
	es.add("/mnt/raid0/layered2/meas2-%05i-00000.cr2");
	es.check();
	if (es.size() == 0)
		throw std::runtime_error("No input found / list of exposures to merge is empty!");
	es.load();
	merge(es);

	writeOpenEXR("test.exr", es.width, es.height, 1, es.image, es.metadata, false);
	return 0;
}
