## hdrmerge
*hdrmerge* is a scientific HDR merging tool: its goal is to create images that
are accurate linear measurements of the radiance received by a camera capable
of producing RAW output. It does not do any fancy noicy removal or other types
of postprocessing and instead tries to be simple, understandable and hackable.

### Compiling:
You will need a recent C++ compiler with support for C++11. g++4.8 works, others
have not been tested yet.

### Usage
    Syntax: ./hdrmerge [options] <RAW file format string / list of multiple files>
    
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
                                 
      --single                   Write EXR files in single precision instead of 
                                 half precision?
                                 
      --output arg (=output.exr) Name of the output file in OpenEXR format
    
    Note that all options can also be specified permanently by creating a text
    file named 'hdrmerge.cfg' in the current directory. It should contain options
    in key=value format.
    
    Examples:
      Create an OpenEXR file from files specified in printf format.
        $ hdrmerge --output scene.exr scene_%02i.cr2
    
      As above, but explicitly specify the files (in any order):
        $ hdrmerge --output scene.exr scene_001.cr2 scene_002.cr2 scene_003.cr2
