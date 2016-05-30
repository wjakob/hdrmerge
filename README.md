## hdrmerge
*hdrmerge* is a scientific HDR merging tool: its goal is to create images that
are accurate linear measurements of the radiance received by a camera capable
of producing RAW output. It does not do any fancy noicy removal or other types
of postprocessing and instead tries to be simple, understandable and hackable.

### Compiling
You will need a recent C++ compiler with support for C++11 and OpenMP. g++4.8 and
msvc 12.0 has been tested so far. This program depends on: libxml2 (for Exiv2), libjpeg,
OpenEXR, libexiv2 and Boost.

Run

    cmake .
    
followed by

    make
    
to start the compilation.

### Usage
    Syntax: ./hdrmerge [options] <RAW file format string / list of multiple files>
    
    Motivation:
      hdrmerge is a scientific HDR merging tool: its goal is to create images that
      are accurate linear measurements of the radiance received by the camera.
      It does not do any fancy noicy removal or other types of postprocessing
      and instead tries to be simple, understandable and hackable.
    
    Summary:
      This program takes an exposure series of DNG/CR2/.. RAW files and merges it
      into a high dynamic-range EXR image. Given a printf-style format expression
      for the input file names, the program automatically figures out both the
      number of images and their exposure times. Any metadata (e.g. lens data)
      present in the input RAW files is also copied over into the output EXR file.
      The program automatically checks for common mistakes like duplicate exposures,
      leaving autofocus or auto-ISO turned on by accident, and it can do useful 
      operations like cropping, resampling, and removing vignetting. Used with 
      just a single image, it works a lot like a hypothetical 'dcraw' in floating
      point mode. OpenMP is used wherever possible to accelerate image processing.
      Note that this program makes the assumption that the input frames are well-
      aligned so that no alignment correction is necessary.
    
      The order of operations is as follows (all steps except 1 and 10 are
      optional; brackets indicate steps that disabled by default):
    
        1. Load RAWs -> 2. HDR Merge -> 3. Demosaic -> 4. Transform colors -> 
        5. [White balance] -> 6. [Scale] -> 7. [Remove vignetting] -> 8. [Crop] -> 
        9. [Resample] -> 10. [Flip/rotate] -> 11. Write OpenEXR
    
    The following sections contain additional information on some of these steps.
    
    Step 1: Load RAWs
      hdrmerge uses the RawSpeed library to support a wide range of RAW formats.
      For simplicity, HDR processing is currently restricted to sensors having a
      standard RGB Bayer grid. From time to time, it may be necessary to update
      the RawSpeed source code to support new camera models. To do this, run the
      'rawspeed/update_rawspeed.sh' shell script and recompile.
    
    Step 2: Merge
      Exposures are merged based on a simple Poisson noise model. In other words,
      the exposures are simply summed together and divided by the total exposure.
      time. To avoid problems with over- and under-exposure, each pixel is
      furthermore weighted such that only well-exposed pixels contribute to this
      summation.
    
      For this procedure, it is crucial that hdrmerge knows the correct exposure
      time for each image. Many cameras today use exposure values that are really
      fractional powers of two rather than common rounded values (i.e. 1/32 as 
      opposed to 1/30 sec). hdrmerge will try to retrieve the true exposure value
      from the EXIF tag. Unfortunately, some cameras "lie" in their EXIF tags
      and use yet another set of exposure times, which can seriously throw off
      the HDR merging process. If your camera does this, pass the parameter 
      --fitexptimes to manually estimate the actual exposure times from the 
      input set of images.
 
      A subtle issue that one should be aware of is that even professional-grade
      lenses from the big two SLR manufactorers tend to have rather inaccurate 
      apertures. Take a photo sequence of a still scene at identical camera
      settings, and you will notice that there is a perceptible amount of flicker
      when turning it into a movie. This is because the aperture radius in each
      shot may vary by a random amount that could be as large as 5%. This is not
      not much of an issue if you're just doing video capture or still
      photography, hence lens manufacturers don't correct for it. But it can
      cause significant headaches in long capture sessions, where it introduces
      random intensity scale factors from image to image. There are two
      workarounds: 1. shoot wide open, or 2. use a trick used by time-lapse
      photographers that is referred to as 'lens twist' or 'aperture twist'
      (search for these keywords online to find videos that demonstrate
      how it works).

    Step 3: Demosaic
      This program uses Adaptive Homogeneity-Directed demosaicing (AHD) to
      interpolate colors over the image. Importantly, demosaicing is done *after*
      HDR merging, on the resulting floating point-valued Bayer grid.
    
    Step 7: Vignetting correction
      To remove vignetting from your photographs, take a single well-exposed 
      picture of a uniformly colored object. Ideally, take a picture through 
      the opening of an integrating sphere, if you have one. Then run hdrmerge
      on this picture using the --vcal parameter. This fits a radial polynomial
      of the form 1 + ax^2 + bx^4 + cx^6 to the image and prints out the
      coefficients. These can then be passed using the --vcorr parameter
    
    Step 9: Resample
      This program can do high quality Lanczos resampling to get lower resolution
      output if desired. This can sometimes cause ringing on high frequency edges,
      in which case a tent filter may be preferable (selectable via --rfilter).
    
    Command line options:
      --help                     Print information on how to use this program
                                 
      --config arg               Load the configuration file 'arg' as an additional
                                 source of command line parameters. Should contain 
                                 one parameter per line in key=value format. The 
                                 command line takes precedence when an argument is 
                                 specified multiple times.
                                 
      --saturation arg           Saturation threshold of the sensor: the ratio of 
                                 the sensor's theoretical dynamic range, at which 
                                 saturation occurs in practice (in [0,1]). 
                                 Estimated automatically if not specified.
                                 
      --fitexptimes              On some cameras, the exposure times in the EXIF 
                                 tags can't be trusted. Use this parameter to 
                                 estimate them automatically for the current image 
                                 sequence
                                 
      --exptimes arg             Override the EXIF exposure times with a manually 
                                 specified sequence of the format 
                                 'time1,time2,time3,..'
                                 
      --nodemosaic               If specified, the raw Bayer grid is exported as a 
                                 grayscale EXR file
                                 
      --colormode arg (=sRGB)    Output color space (one of 'native'/'sRGB'/'XYZ')
                                 
      --sensor2xyz arg           Matrix that transforms from the sensor color space
                                 to XYZ tristimulus values
                                 
      --scale arg                Optional scale factor that is applied to the image
                                 
      --crop arg                 Crop to a rectangular area. 'arg' should be 
                                 specified in the form x,y,width,height
                                 
      --resample arg             Resample the image to a different resolution. 
                                 'arg' can be a pair of integers like 1188x790 or 
                                 the max. resolution (maintaining the aspect ratio)
                                 
      --rfilter arg (=lanczos)   Resampling filter used by the --resample option 
                                 (available choices: 'tent' or 'lanczos')
                                 
      --wbalpatch arg            White balance the image using a grey patch 
                                 occupying the region 'arg' (specified as 
                                 x,y,width,height). Prints output suitable for 
                                 --wbal
                                 
      --wbal arg                 White balance the image using floating point 
                                 multipliers 'arg' specified as r,g,b
                                 
      --vcal                     Calibrate vignetting correction given a uniformly 
                                 illuminated image
                                 
      --vcorr arg                Apply the vignetting correction computed using 
                                 --vcal
                                 
      --flip arg                 Flip the output image along the specified axes 
                                 (one of 'x', 'y', or 'xy')
                                 
      --rotate arg (=0)          Rotate the output image by 90, 180 or 270 degrees
                                 
      --format arg (=half)       Choose the desired output file format -- one of 
                                 'half' (OpenEXR, 16 bit HDR / half precision), 
                                 'single' (OpenEXR, 32 bit / single precision), 
                                 'jpeg' (libjpeg, 8 bit LDR for convenience)
                                 
      --output arg (=output.exr) Name of the output file in OpenEXR format. When 
                                 only a single RAW file is processed, its name is 
                                 used by default (with the ending replaced by 
                                 .exr/.jpeg
    
    Note that all options can also be specified permanently by creating a text
    file named 'hdrmerge.cfg' in the current directory. It should contain options
    in key=value format.
    
    Examples:
      Create an OpenEXR file from files specified in printf format.
        $ hdrmerge --output scene.exr scene_%02i.cr2
    
      As above, but explicitly specify the files (in any order):
        $ hdrmerge --output scene.exr scene_001.cr2 scene_002.cr2 scene_003.cr2
### License
hdrmerge is licensed under the GNU General Public License (Version 3),
which can be retrieved at the following address: http://www.gnu.org/licenses/gpl-3.0.txt
