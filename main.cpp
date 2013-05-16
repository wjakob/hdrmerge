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
		("colormatrix", po::value<std::string>()->multitoken(), "Matrix that transforms from the sensor color space to linear sRGB")
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
		po::store(po::command_line_parser(argc, argv).options(all_options).positional(positional).run(), vm);
		if (vm.count("help")) {
			cout << "Command line parameters" << endl << options;
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

	float colormatrix[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1 };
	if (!vm.count("colormatrix")) {
		cerr << "Warning: no color matrix was specified -- this is necessary to get proper sRGB" << endl
			 << "output. To acquire this matrix, convert any one of your RAW images to a DNG file" << endl
			 << "using Adobe's DNG converter on Windows (or on Linux, using 'wine'). Then run" << endl
			 << endl
			 << "  $ exiv2 -pt image.dng 2> /dev/null | grep ColorMatrix2" << endl
			 << "  Exif.Image.ColorMatrix2 SRational 9  <sequence of ratios>" << endl
			 << endl
			 << "Next, turn this into a space/comma-separated sequence of floating point values " << endl
			 << "and add a line to the file hdrmerge.cfg (creating it if necessary), like so:" << endl
			 << endl
			 << "  colormatrix=0.4920 0.0616 -0.0593 -0.6493 1.3964 0.2784 -0.1774 0.3178 0.7005" << endl
			 << endl
			 << "To get output in the camera native color space, use a linear matrix:" << endl
			 << endl
			 << "  colormatrix=1 0 0 0 1 0 0 0 1" << endl
			 << endl
			 << "(reverting to camera native color space for now..)" << endl;
	} else {
		std::string argument = vm["colormatrix"].as<std::string>();
		boost::char_separator<char> sep(", ");
		boost::tokenizer<boost::char_separator<char>> tokens(argument, sep);

		try {
			int pos = -1;
			for (auto it = tokens.begin(); it != tokens.end(); ++it) {
				if (++pos == 9)
					break;
    			colormatrix[pos] = boost::lexical_cast<float>(*it);
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

		es.demosaic(colormatrix);
	} catch (const std::exception &ex) {
		cerr << "Encountered a fatal error: " << ex.what() << endl;
		return -1;
	}

	return 0;
}
