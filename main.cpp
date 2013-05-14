#include "hdrmerge.h"

void merge(ExposureSeries &es) {
	int x = (int) (randf() * es.width);
	int y = (int) (randf() * es.height);

	cout << "x=" << x << ", y=" << y << endl;

	cout << "[";
	for (int i=0; i<es.size(); ++i) {
		cout << es.eval(i, x, y);
		if (i+1 < es.size())
			cout << ", ";
	}
	cout << "]" << endl;

	cout << "[";
	for (int i=0; i<es.size(); ++i) {
		cout << es.exposures[i].exposure;
		if (i+1 < es.size())
			cout << ", ";
	}
	cout << "]" << endl;

}

int main(int argc, char **argv) {
	ExposureSeries es;
//	es.add("test-%02i.cr2");
	es.add("/mnt/raid0/layered2/meas-%05i-00000.cr2");
	es.check();
	if (es.size() == 0)
		throw std::runtime_error("No input found / list of exposures to merge is empty!");
	es.load();

	for (int i=0; i<10; ++i) {
		merge(es);
	}


	writeOpenEXR("test.exr", es.width, es.height, 1, es.exposures[2].image, es.metadata, false);
	return 0;
}
