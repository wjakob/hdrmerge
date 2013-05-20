#include "hdrmerge.h"
#include "Eigen/QR"
#include <fstream>

/// Evaluate all Bernstein polynomials up to a certain order at 'x'
template <typename Derived> void bernstein(const Eigen::MatrixBase<Derived> &_vec, double x) {
	Eigen::MatrixBase<Derived> &vec = const_cast<Eigen::MatrixBase<Derived>&>(_vec);
	int size = vec.size();

	Eigen::Map<Eigen::VectorXd> temp((double *) alloca(size * sizeof(double)), size);
	vec[0] = 1;

	for (int i=1; i<size; ++i) {
		temp[i] = 0;
		temp.head(i)        = vec.head(i) * (1-x);
		temp.segment(1, i) += vec.head(i) * x;
		vec.head(i+1)       = temp.head(i+1);
	}
}

/**
 * Simple data structure to keep track of fixed-size approximately
 * constant image patches that will be used to recover the camera
 * response function
 */
struct Patch {
	static const size_t patch_size = 20;
	size_t x, y;

	/// Default dummy constructor
	inline Patch() { }

	/// Randomly sample a patch position
	inline Patch(const ExposureSeries &es) {
		x = 2 * (size_t) (randf() * (es.width  - 4*patch_size)/2) + patch_size;
		y = 2 * (size_t) (randf() * (es.height - 4*patch_size)/2) + patch_size;
	}

	void computeStatistics(const ExposureSeries &es, int img, float *min, float *max, float *rel_stddev) const {
		float mean[3], variance[3];
		int count[3];
		
		for (int i=0; i<3; ++i) {
			min[i]      =  std::numeric_limits<float>::infinity();
			max[i]      = -std::numeric_limits<float>::infinity();
			variance[i] = 0;
			mean[i]     = 0;
			count[i]    = 0;
		}

		for (size_t yo=0; yo<patch_size; ++yo) {
			for (size_t xo=0; xo<patch_size; ++xo) {
				int color = es.fc(x+xo, y+yo);
				float value = es.eval(img, x+xo, y+yo);
				min[color] = std::min(min[color], value);
				max[color] = std::max(max[color], value);
				mean[color] += value;
				count[color]++;
			}
		}

		for (int i=0; i<3; ++i)
			mean[i] /= count[i];

		for (size_t yo=0; yo<patch_size; ++yo) {
			for (size_t xo=0; xo<patch_size; ++xo) {
				int color = es.fc(x+xo, y+yo);
				float diff = es.eval(img, x+xo, y+yo)-mean[color];
				variance[color] += diff*diff;
			}
		}
	
		for (int i=0; i<3; ++i)
			rel_stddev[i] = std::sqrt(variance[i] / (count[i]-1)) / std::abs(mean[i]);
	}

	void computeMean(const ExposureSeries &es, int img, float *mean) const {
		int count[3] = { 0, 0, 0 };
		memset(mean, 0, sizeof(float)*3);
		for (size_t yo=0; yo<patch_size; ++yo) {
			for (size_t xo=0; xo<patch_size; ++xo) {
				int color = es.fc(x+xo, y+yo);
				mean[color] += es.eval(img, x+xo, y+yo);
				count[color]++;
			}
		}

		for (int i=0; i<3; ++i)
			mean[i] /= count[i];
	}

	/// Heuristic for deciding whether or not a patch is "good"
	bool isGood(const ExposureSeries &es, int img, int ch) const {
		float min[3], max[3], rel_stddev[3];
		computeStatistics(es, img, min, max, rel_stddev);

		return 
			min[ch] > 0.01 &&
			max[ch] < es.saturation-0.05 &&
			rel_stddev[ch] < 0.1f;
	}

	/// Does a patch overlap another patch?
	bool overlaps(const Patch &p) const {
		return std::abs(x-p.x) < patch_size &&
		       std::abs(y-p.y) < patch_size;
	}
};
	
void ExposureSeries::fitExposureTimes() {
	const int patches_per_exposure = 200,
	          max_tries            = patches_per_exposure * 100,
			  channel              = 1; // Use green channel for the estimation

	std::vector<Patch> patches, patchList;
	std::vector<bool> good(exposures.size());
	int good_exposures = 0;

	cout << "Fitting exposure times .. " << endl;
	for (size_t img=0; img<exposures.size(); ++img) {
		patches.erase(std::remove_if(patches.begin(), patches.end(),
			[&](const Patch &p) { return !p.isGood(*this, img, channel); }), patches.end());

		int tries = 0;
		for (tries=0; tries<max_tries; ++tries) {
			if ((int) patches.size() == patches_per_exposure)
				break;

			Patch patch(*this);

			/* Phase 1: is the sample good? */
			if (!patch.isGood(*this, img, channel))
				continue;

			/* Phase 2: overlap test (could be accelerated, oh well..) */
			bool valid = true;
			for (size_t i=0; i<patches.size(); ++i) {
				if (patch.overlaps(patches[i])) {
					valid = false;
					break;
				}
			}
			if (!valid)
				continue;

			patches.push_back(patch);
			patchList.push_back(patch);
		}

		good[img] = (patches.size() == (size_t) patches_per_exposure);
		cout << "  - Exposure " << img << ": found " << patches.size()
		     << " well-exposed uniform patches after " << tries << " tries." << endl;
		if (!good[img])
			cerr << "    Warning: not enough patches found -- consider removing this" << endl
			     << "    exposure (excluding from the fit)" << endl;
		else
			++good_exposures;
	}

	
	if (good_exposures < 3)
		throw std::runtime_error("Less than 3 good exposures ..  this is not going to work!");

	size_t nRows = 0;
	for (size_t i=0; i<patchList.size(); ++i)
		for (size_t img=0; img<exposures.size(); ++img)
			if (good[img] && patchList[i].isGood(*this, img, channel))
				++nRows;

	Eigen::MatrixXd A(nRows + 1, good_exposures + patchList.size());
	Eigen::VectorXd b(nRows + 1);
	A.setZero();
	b.setZero();

	size_t row = 0;
	for (size_t i=0; i<patchList.size(); ++i) {
		int exposure_idx = 0;
		for (size_t img=0; img<exposures.size(); ++img) {
			if (!good[img])
				continue;
			if (patchList[i].isGood(*this, img, channel)) {
				A(row, exposure_idx) = 1;
				A(row, good_exposures + i) = 1;

				float mean[3];
				patchList[i].computeMean(*this, img, mean);
				b(row) = std::log(mean[channel]) / std::log(2);
				row++;
			}
			++exposure_idx;
		}
	}
	
	float longestExposure;
	for (size_t img=0; img<exposures.size(); ++img) {
		if (!good[img])
			continue;
		longestExposure = exposures[img].exposure;
	}

	cout << "  - Assuming that the " << longestExposure << "s exposure is accurate (and computing the" << endl
		 << "    other exposure times with respect to it)" << endl;

	A(nRows, good_exposures-1) = 1;
	b(nRows) = std::log(longestExposure) / std::log(2);
	Eigen::VectorXd result = A.colPivHouseholderQr().solve(b);

	size_t index = 0;
	std::vector<float> exposuretimes_old(exposures.size());
	for (size_t img=0; img<exposures.size(); ++img) {
		exposuretimes_old[img] = exposures[img].exposure;
		if (!good[img])
			continue;
		exposures[img].exposure = std::pow(2.0, result[index]);
		index++;
	}
	cout << endl;
	cout << "Fitting is done. To cause hdrmerge to use these corrected exposure times in" << endl
	     << "future sessions, add the following line to hdrmerge.cfg:" << endl
		 << endl
		 << "exptimes = ";
	for (size_t img=0; img<exposures.size(); ++img) {
		cout << exposures[img].exposure;
		if (img+1 < exposures.size())
			cout << ", ";
	}
	cout << endl << endl;

	cout << "To verify the quality of this fit, execute the script 'exptime_showfit.m' in" << endl
	     << "MATLAB or Octave. The data points should nicely align to the diagonal." << endl
		 << endl;

	{
		std::ofstream os("exptime_showfit.m");
		os.precision(10);

		os << "datapoints=[";
		for (size_t patch_idx=0; patch_idx<patchList.size(); ++patch_idx) {
			const Patch &patch = patchList[patch_idx];
			for (size_t img=0; img<exposures.size(); ++img) {
				if (!patch.isGood(*this, img, channel))
					continue;

				float mean[3];
				patch.computeMean(*this, img, mean);
				float x = mean[channel];
				float y = std::pow(2.0f, result(good_exposures + patch_idx)) * exposures[img].exposure;
				float z = std::pow(2.0f, result(good_exposures + patch_idx)) * exposuretimes_old[img];
				os << x << ", " << y  << ", " << z << "; ";
			}
		}

		os << "];";
		os << "subplot(2,1,1)" << endl;
		os << "plot(datapoints(:,3), datapoints(:, 1), '.');" << endl;
		os << "hold on;" << endl;
		os << "title('Exposure times provided by the EXIF tags');" << endl;
		os << "plot([0 1],[0 1], 'r');" << endl;
		os << "subplot(2,1,2)" << endl;
		os << "plot(datapoints(:,2), datapoints(:, 1), '.');" << endl;
		os << "hold on;" << endl;
		os << "title('Fitted exposure times');" << endl;
		os << "plot([0 1],[0 1], 'r');" << endl;
	}
}
