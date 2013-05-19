#include <boost/program_options.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/tokenizer.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <fstream>
#include "hdrmerge.h"

namespace po = boost::program_options;

enum EColorMode {
	ENative,
	ESRGB,
	EXYZ
};

std::istream& operator>>(std::istream& in, EColorMode& unit) {
	std::string token;
	in >> token;
	std::string token_lc = boost::to_lower_copy(token);

	if (token_lc == "native")
		unit = ENative;
	else if (token_lc == "srgb")
		unit = ESRGB;
	else if (token_lc == "xyz")
		unit = EXYZ;
	else
		throw po::validation_error(po::validation_error::invalid_option_value, "colormode", token);
	return in;
}

/// Windowed Lanczos filter
class LanczosSincFilter : public ReconstructionFilter {
public:
	LanczosSincFilter(float radius = 3) : m_radius(radius) { }

	float getRadius() const {
		return m_radius;
	}

	float eval(float x) const {
		x = std::abs(x);

		if (x < 1e-4f)
			return 1.0f;
		else if (x > m_radius)
			return 0.0f;

		float x1 = M_PI * x;
		float x2 = x1 / m_radius;

		return (std::sin(x1) * std::sin(x2)) / (x1 * x2);
	}
private:
	float m_radius;
};

/// Tent filter
class TentFilter : public ReconstructionFilter {
public:
	TentFilter(float radius = 1) : m_radius(radius) { }

	float getRadius() const {
		return m_radius;
	}

	float eval(float x) const {
		return std::max(0.0f, 1.0f - std::abs(x / m_radius));
	}
private:
	float m_radius;
};

template <typename T> std::vector<T> parse_list(const po::variables_map &vm, 
		const std::string &name, const std::vector<size_t> &nargs, 
		const char *sepstr = " ,") {
	std::vector<T> result;

	if (!vm.count(name))
		return result;

	std::string argument = vm[name].as<std::string>();
	boost::char_separator<char> sep(sepstr);
	boost::tokenizer<boost::char_separator<char>> tokens(argument, sep);

	try {
		for (auto it = tokens.begin(); it != tokens.end(); ++it)
			result.push_back(boost::lexical_cast<T>(*it));
	} catch (const boost::bad_lexical_cast &) {
		throw std::runtime_error((boost::format("Unable to parse the '%1%' argument!") % name).str());
	}

	bool good = false;
	std::ostringstream oss;
	for (size_t i=0; i<nargs.size(); ++i) {
		if (result.size() == nargs[i]) {
			good = true;
			break;
		}
		oss << nargs[i];
		if (i+1 < nargs.size())
			oss << " or ";
	}

	if (!good)
		throw std::runtime_error((boost::format("Unable to parse the '%1%'"
			" argument -- expected %2% values!") % name % oss.str()).str());

	return result;
}

void help(char **argv, const po::options_description &desc) {
	cout << "RAW to HDR merging tool, written by Wenzel Jakob <wenzel@cs.cornell.edu>" << endl
		<< "Version 1.0 (May 2013). Source @ https://github.com/wjakob/hdrmerge" << endl
		<< endl
		<< "Syntax: " << argv[0] << " [options] <RAW file format string / list of multiple files>" << endl
		<< endl
		<< "Summary:"<< endl
		<< "  This program takes an exposure series of DNG/CR2/.. RAW files and merges it" << endl
		<< "  into a high dynamic-range EXR image. Given a printf-style format expression" << endl
		<< "  for the input file names, the program automatically figures out both the" << endl
		<< "  number of images and their exposure times. Any metadata (e.g. lens data)" << endl
		<< "  present in the input RAW files is also copied over into the output EXR file." << endl
		<< "  The program automatically checks for common mistakes like duplicate exposures," << endl
		<< "  leaving autofocus or auto-ISO turned on by accident, and it can do useful " << endl
		<< "  operations like cropping, resampling, and removing vignetting. Used with " << endl
		<< "  just a single image, it works a lot like a hypothetical 'dcraw' in floating" << endl
		<< "  point mode." << endl
		<< endl
		<< "  The order of operations is as follows (all steps except 1 and 10 are optional" << endl
		<< "  brackets indicate ones that disabled by default):" << endl << endl
		<< "    1. Load RAWs -> 2. HDR Merge -> 3. Demosaic -> 4. Transform colors -> " << endl
		<< "    5. [Whitebalance] -> 6. [Scale] -> 7. [Remove vignetting] -> 8. [Crop] -> " << endl
		<< "    9. [Resample] -> 8. Write OpenEXR" << endl
		<< endl
		<< "The following sections contain additional information on some of these steps." << endl
		<< endl
		<< "Step 1: Load RAWs" << endl
		<< "  hdrmerge uses the RawSpeed library to support a wide range of RAW formats." << endl
		<< "  For simplicity, HDR processing is currently restricted to sensors having a" << endl
		<< "  standard RGB Bayer grid. From time to time, it may be necessary to update" << endl
		<< "  the RawSpeed source code to support new camera models. To do this, run the" << endl
		<< "  'rawspeed/update_rawspeed.sh' shell script and recompile." << endl
		<< endl
		<< "Step 2: Merge" << endl
		<< "  TBD" << endl
		<< endl
		<< "Step 7: Vignetting correction" << endl
		<< "  To remove vignetting from your photographs, take a single well-exposed " << endl
		<< "  picture of a uniformly colored object. Ideally, take a picture through " << endl
		<< "  the opening of an integrating sphere, if you have one. Then run hdrmerge" << endl
		<< "  on this picture using the --vcal parameter. This fits a radial polynomial" << endl
		<< "  of the form 1 + ax^2 + bx^4 + cx^6 to the image and prints out the" << endl
		<< "  coefficients. These can then be passed using the --vcorr parameter" << endl
		<< endl
		<< "Step 9: Resample" << endl
		<< "  This program can do high quality Lanczos resampling to get lower resolution" << endl
		<< "  output if desired. This can sometimes cause ringing on high frequency edges," << endl
		<< "  in which case a Tent filter may be preferable (selectable via --rfilter)." << endl
		<< endl
		<< desc << endl
	    << "Note that all options can also be specified permanently by creating a text" << endl
		<< "file named 'hdrmerge.cfg' in the current directory. It should contain options" << endl
		<< "in key=value format." << endl
		<< endl
	    << "Examples:" << endl
		<< "  Create an OpenEXR file from files specified in printf format." << endl
		<< "    $ hdrmerge --output scene.exr scene_%02i.cr2" << endl
		<< endl
		<< "  As above, but explicitly specify the files (in any order):" << endl
		<< "    $ hdrmerge --output scene.exr scene_001.cr2 scene_002.cr2 scene_003.cr2" << endl;
}

int main(int argc, char **argv) {
	po::options_description options("Command line options");
	po::options_description hidden_options("Hiden options");
	po::variables_map vm;

	options.add_options()
		("help", "Print information on how to use this program\n")
		("nodemosaic", "If specified, the raw Bayer grid is exported as a grayscale EXR file\n")
		("colormode", po::value<EColorMode>()->default_value(ESRGB, "sRGB"),
			"Output color space (one of 'native'/'sRGB'/'XYZ')\n")
		("sensor2xyz", po::value<std::string>(),
			"Matrix that transforms from the sensor color space to XYZ tristimulus values\n")
		("scale", po::value<float>(),
			"Optional scale factor that is applied to the image\n")
		("crop", po::value<std::string>(),
			"Crop to a rectangular area. 'arg' should be specified in the form x,y,width,height\n")
		("resample", po::value<std::string>(),
			"Resample the image to a different resolution. 'arg' can be "
			"a pair of integers like 1188x790 or the max. resolution ("
			"maintaining the aspect ratio)\n")
		("rfilter", po::value<std::string>()->default_value("lanczos"),
			"Resampling filter used by the --resample option (available choices: "
			"'tent' or 'lanczos')\n")
		("wbalpatch", po::value<std::string>(),
		    "White balance the image using a grey patch occupying the region "
			"'arg' (specified as x,y,width,height). Prints output suitable for --wbal\n")
		("wbal", po::value<std::string>(),
		    "White balance the image using floating point multipliers 'arg' "
			"specified as r,g,b\n")
		("vcal", "Calibrate vignetting correction given a uniformly illuminated image.")
		("vcorr", po::value<std::string>(),
		    "Apply the vignetting correction computed using --vcal")
		("single", "Write EXR files in single precision instead of half precision?\n")
		("output", po::value<std::string>()->default_value("output.exr"),
			"Name of the output file in OpenEXR format");

	hidden_options.add_options()
		("input-files", po::value<std::vector<std::string>>(), "Input files");

	po::options_description all_options;
	all_options.add(options).add(hidden_options);

	try {
		std::ifstream settings("hdrmerge.cfg", std::ifstream::in);
		po::store(po::parse_config_file(settings, all_options), vm);
		settings.close();

		po::positional_options_description positional;
		positional.add("input-files", -1);
		po::store(po::command_line_parser(argc, argv)
			.options(all_options).positional(positional).run(), vm);
		if (vm.count("help") || !vm.count("input-files")) {
			help(argv, options);
			return 0;
		}
		po::notify(vm);
	} catch (po::error &e) {
		cerr << "Error while parsing command line arguments: " << e.what() << endl << endl;
		help(argv, options);
		return -1;
	}

	try {
		EColorMode colormode = vm["colormode"].as<EColorMode>();
		std::vector<int> wbalpatch      = parse_list<int>(vm, "wbalpatch", { 4 });
		std::vector<float> wbal         = parse_list<float>(vm, "wbal", { 3 });
		std::vector<int> resample       = parse_list<int>(vm, "resample", { 1, 2 }, ", x");
		std::vector<int> crop           = parse_list<int>(vm, "crop", { 4 });
		std::vector<float> sensor2xyz_v = parse_list<float>(vm, "sensor2xyz", { 9 });
		std::vector<float> vcorr        = parse_list<float>(vm, "vcorr", { 3 });

		if (!wbal.empty() && !wbalpatch.empty()) {
			cerr << "Cannot specify --wbal and --wbalpatch at the same time!" << endl;
			return -1;
		}

		float sensor2xyz[9] = {
			0.412453f, 0.357580f, 0.180423f,
			0.212671f, 0.715160f, 0.072169f,
			0.019334f, 0.119193f, 0.950227f
		};

		if (!sensor2xyz_v.empty()) {
			for (int i=0; i<9; ++i)
				sensor2xyz[i] = sensor2xyz_v[i];
		} else if (colormode != ENative) {
			cerr << "*******************************************************************************" << endl
				<< "Warning: no sensor2xyz matrix was specified -- this is necessary to get proper" << endl
				<< "sRGB / XYZ output. To acquire this matrix, convert any one of your RAW images" << endl
				<< "into a DNG file using Adobe's DNG converter on Windows (or on Linux, using the" << endl
				<< "'wine' emulator). The run" << endl
				<< endl
				<< "  $ exiv2 -pt the_image.dng 2> /dev/null | grep ColorMatrix2" << endl
				<< "  Exif.Image.ColorMatrix2 SRational 9  <sequence of ratios>" << endl
				<< endl
				<< "The sequence of a rational numbers is a matrix in row-major order. Compute its" << endl
				<< "inverse using a tool like MATLAB or Octave and add a matching entry to the" << endl
				<< "file hdrmerge.cfg (creating it if necessary), like so:" << endl
				<< endl
				<< "# Sensor to XYZ color space transform (Canon EOS 50D)" << endl
				<< "sensor2xyz=1.933062 -0.1347 0.217175 0.880916 0.725958 -0.213945 0.089893 " << endl
				<< "-0.363462 1.579612" << endl
				<< endl
				<< "-> Providing output in the native sensor color space, as no matrix was given." << endl
				<< "*******************************************************************************" << endl;

			colormode = ENative;
		}

		std::vector<std::string> exposures = vm["input-files"].as<std::vector<std::string>>();
		float scale = 1.0f;
		if (vm.count("scale"))
			scale = vm["scale"].as<float>();

		// Step 1: Load RAW
		ExposureSeries es;
		for (size_t i=0; i<exposures.size(); ++i)
			es.add(exposures[i]);
		es.check();
		if (es.size() == 0)
			throw std::runtime_error("No input found / list of exposures to merge is empty!");
		es.load();

		/// Step 2: HDR merge
		es.merge();

		/// Step 3: Demosaicing
		bool demosaic = vm.count("nodemosaic") == 0;
		if (demosaic)
			es.demosaic(sensor2xyz);

		/// Step 4: Transform colors
		if (colormode != ENative) {
			if (!demosaic) {
				cerr << "Warning: you requested XYZ/sRGB output, but demosaicing was explicitly disabled! " << endl
					 << "Color processing is not supported in this case -- writing raw sensor colors instead." << endl;
			} else {
				es.transform_color(sensor2xyz, colormode == EXYZ);
			}
		}

		/// Step 5: White balancing
		if (!wbal.empty()) {
			float scale[3] = { wbal[0], wbal[1], wbal[2] };
			es.whitebalance(scale);
		} else if (wbalpatch.size()) {
			es.whitebalance(wbalpatch[0], wbalpatch[1], wbalpatch[2], wbalpatch[3]);
		}

		/// Step 6: Scale
		if (scale != 1.0f)
			es.scale(scale);

		/// Step 7: Remove vignetting
		if (vm.count("vcal")) {
			if (vm.count("vcorr")) {
				cerr << "Warning: only one of --vcal and --vcorr can be specified at a time. Ignoring --vcorr" << endl;
			}
			es.vcal();
		} else if (!vcorr.empty()) {
			es.vcorr(vcorr[0], vcorr[1], vcorr[2]);
		}

		/// Step 8: Crop
		if (!crop.empty())
			es.crop(crop[0], crop[1], crop[2], crop[3]);

		/// Step 9: Resample
		if (!resample.empty()) {
			int w, h;

			if (resample.size() == 1) {
				float factor = resample[0] / (float) std::max(es.width, es.height);
				w = (int) std::round(factor * es.width);
				h = (int) std::round(factor * es.height);
			} else {
				w = resample[0];
				h = resample[1];
			}

			if (demosaic) {
				std::string rfilter = boost::to_lower_copy(vm["rfilter"].as<std::string>());
				if (rfilter == "lanczos") {
					es.resample(LanczosSincFilter(), w, h);
				} else if (rfilter == "tent") {
					es.resample(TentFilter(), w, h);
				} else {
					cout << "Invalid resampling filter chosen (must be 'lanczos' / 'tent')" << endl;
					return -1;
				}
			} else {
				cout << "Warning: resampling a non-demosaiced image does not make much sense -- ignoring." << endl;
			}
		}

		// Step 10: Write output
		std::string output = vm["output"].as<std::string>();
		bool half = vm.count("single") == 0;

		if (demosaic) {
			writeOpenEXR(output, es.width, es.height, 3,
				(float *) es.image_demosaiced, es.metadata, half);
		} else {
			writeOpenEXR(output, es.width, es.height, 1,
				(float *) es.image_merged, es.metadata, half);
		}
	} catch (const std::exception &ex) {
		cerr << "Encountered a fatal error: " << ex.what() << endl;
		return -1;
	}

	return 0;
}
