#include <boost/program_options.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/tokenizer.hpp>
#include <boost/algorithm/string.hpp>
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

void help(char **argv, const po::options_description &desc) {
	cout << "RAW to EXR conversion tool, written by Wenzel Jakob <wenzel@cs.cornell.edu>" << endl
		<< endl
		<< "Syntax: " << argv[0] << " [options] <RAW file format string / list of multiple files>" << endl
		<< endl
		<< "Summary:"<< endl
		<< "  This program takes an exposure series of CR2 or DNG files and merges it into" << endl
		<< "  a high dynamic-range EXR image. Given a printf-style format expression for the" << endl
		<< "  input file names, the program automatically figures out both the number of" << endl
		<< "  images and their exposure times. Any metadata (e.g. lens data) present in the" << endl
		<< "  input RAW files is also copied over into the output EXR file." << endl
		<< endl
		<< desc << endl
	    << "Example:" << endl
		<< "  hdrmerge --output scene.exr scene_%04i.cr2" << endl;
}

int main(int argc, char **argv) {
	po::options_description options("Command line options");
	po::options_description hidden_options("Hiden options");
	po::variables_map vm;

	options.add_options()
		("help", "Print information on how to use this program")
		("output", po::value<std::string>()->default_value("output.exr"), 
			"Name of the output file in OpenEXR format")
		("scale", po::value<float>(),
			"Optional scale factor that is applied to the image")
		("colormode", po::value<EColorMode>()->default_value(ESRGB, "sRGB"),
			"Output color space (one of 'native'/'sRGB'/'XYZ')")
		("sensor2xyz", po::value<std::string>()->multitoken(), 
			"Matrix that transforms from the sensor color space to XYZ tristimulus values")
		("demosaic", po::value<bool>()->default_value(true, "yes"),
			"Perform demosaicing? If disabled, the raw Bayer grid is exported as a grayscale EXR file")
		("half", po::value<bool>()->default_value(true, "yes"),
			"To save storage, hdrmerge writes half precision files by "
			"default (set to 'no' for single precision)");

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

	float sensor2xyz[9] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	EColorMode colormode = vm["colormode"].as<EColorMode>();

	if (!vm.count("sensor2xyz") && colormode != ENative) {
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
	} else {
		std::string argument = vm["sensor2xyz"].as<std::string>();
		boost::char_separator<char> sep(", ");
		boost::tokenizer<boost::char_separator<char>> tokens(argument, sep);

		try {
			int pos = -1;
			for (auto it = tokens.begin(); it != tokens.end(); ++it) {
				if (++pos == 9)
					break;
    			sensor2xyz[pos] = boost::lexical_cast<float>(*it);
			}
			if (pos != 8) {
				cerr << "Color matrix argument has the wrong number of entries!" << endl;
				return -1;
			}
		} catch (const boost::bad_lexical_cast &) {
			cerr << "Unable to parse color matrix argument!" << endl;
			return -1;
		}
	}

	try {
		std::vector<std::string> exposures = vm["input-files"].as<std::vector<std::string>>();
		float scale = 1.0f;
		if (vm.count("scale"))
			scale = vm["scale"].as<float>();

		ExposureSeries es;
		for (size_t i=0; i<exposures.size(); ++i)
			es.add(exposures[i]);
		es.check();
		if (es.size() == 0)
			throw std::runtime_error("No input found / list of exposures to merge is empty!");
		es.load();
		es.merge();

		bool demosaic = vm["demosaic"].as<bool>();

		if (demosaic)
			es.demosaic(sensor2xyz);

		if (colormode != ENative) {
			if (!demosaic) {
				cerr << "Warning: you requested XYZ/sRGB output while disabling demosaicing. " << endl
					 << "Color processing is not supported in this case -- writing raw sensor colors instead." << endl;
			} else {
				es.transform_color(sensor2xyz, colormode == EXYZ);
			}
		}

		if (scale != 1.0f)
			es.scale(scale);

		std::string output = vm["output"].as<std::string>();
		bool half = vm["half"].as<bool>();

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
