#include <boost/program_options.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/tokenizer.hpp>
#include <fstream>
#include "hdrmerge.h"

namespace po = boost::program_options;

int main(int argc, char **argv) {
	po::options_description options("Options");
	po::options_description hidden_options("Hiden options");
	po::variables_map vm;

	options.add_options()
		("help", "Print information on how to use this program")
		("sensor2xyz", po::value<std::string>()->multitoken(), 
			"Matrix that transforms from the sensor color space to XYZ")
		("demosaic", po::value<bool>()->default_value(true, "yes"), "Perform demosaicing?");
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
		if (vm.count("help")) {
			cout << "Command line parameters" << endl << options;
			return 0;
		}
		po::notify(vm);
	} catch (po::error &e) {
		cerr << "Error while parsing command line arguments: " << e.what() << endl << endl;
		cout << options << endl;
		return -1;
	}

	if (!vm.count("input-files")) {
		cout << "Command line parameters" << endl << options;
		return 0;
	}

	/* Dummy default which converts from RGB -> XYZ -- this causes the
	   color processing to be the identity transformation */
	float sensor2xyz[9] = { 0.412453, 0.357580, 0.180423, 0.212671, 0.715160, 0.072169, 0.019334, 0.119193, 0.95022 };

	if (!vm.count("sensor2xyz")) {
		cerr << "Warning: no sensor2xyz matrix was specified -- this is necessary to get proper sRGB" << endl
			 << "output. To acquire this matrix, convert any one of your RAW images to a DNG file" << endl
			 << "using Adobe's DNG converter on Windows (or on Linux, using 'wine'). Then run" << endl
			 << endl
			 << "  $ exiv2 -pt the_image.dng 2> /dev/null | grep ColorMatrix2" << endl
			 << "  Exif.Image.ColorMatrix2 SRational 9  <sequence of ratios>" << endl
			 << endl
			 << "Compute the inverse of this matrix (in row-major order) and add add an entry to the"
			 << "file hdrmerge.cfg (creating it if necessary), like so:" << endl
			 << endl
			 << "# Sensor to XYZ color space transform (Canon EOS 50D)"
			 << "sensor2xyz=1.933062 -0.1347 0.217175 0.880916 0.725958 -0.213945 0.089893 -0.363462 1.579612"
			 << endl
			 << "-> Providing output in the native sensor color space, as no matrix was given." << endl;
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

		ExposureSeries es;
		for (size_t i=0; i<exposures.size(); ++i)
			es.add(exposures[i]);
		es.check();
		if (es.size() == 0)
			throw std::runtime_error("No input found / list of exposures to merge is empty!");
		es.load();
		es.merge();

		es.demosaic(sensor2xyz);

		writeOpenEXR("test.exr", es.width, es.height, 3, (float *) es.image_demosaiced, es.metadata, false);

		const float xyz2rgb[3][3] = {
			{ 3.240479f, -1.537150f, -0.498535f },
			{-0.969256f, +1.875991f, +0.041556f },
			{ 0.055648f, -0.204043f, +1.057311f }
		};

	} catch (const std::exception &ex) {
		cerr << "Encountered a fatal error: " << ex.what() << endl;
		return -1;
	}

	return 0;
}
