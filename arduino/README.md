## Notes

For Arduino the source files should be in the src directory, so I symlinked all relevant compliant files 

The Arduino specific new files can be found in the arduino directory
I have also adapted plenty of files so that they compile by addressing the following topics

- Deactivate exceptions (with the help of #defines)
- Use lwip as network interface
- Replace printf(stderr, ...) with a separate LOG() method, so that we can handle them properly in Arduino
- Renamed inet.c to inet.cpp so that we can use c++ classes
- Used a [file astraction layer](https://pschatzmann.github.io/live555/html/class_abstract_file.html#details) to replace the stdio.h operations so that we can use different SD libraries

The biggest challange was with the last point. Unfortunatly Arduino does not provide a proper stdio.h implementation 
that can be used to access files. On the other hand most projects assume that stdio.h is available and therfore do
not use any additional abstraction layer: live555 is here not an exception. 

For the time beeing I concentrate to make the ESP32 compile.
