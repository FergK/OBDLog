# OBDLog

OBDLog is a project I completed as my semester project in my Embedded Systems course for Spring 2017 at Georgia State University.

A command line utility written in C connects to a vehicle's ECU via a OBD-II UART. A Nodejs application uses the command line utility to periodically queries the ECU for various parameters, which are displayed in a web browser in real-time using websockets.

I ran this on a Raspberry Pi Model 3 and used a SparkFun OBD-II UART. A full parts list is available in the /slides directory.

## Videos

[Project Update Video](https://youtu.be/VoDeGGIunfs): Some background on why I chose this project. This video was turned in early in the semester to outline my goals.

[Project Demonstration Video](https://youtu.be/fdGgWg5rqpk): This video summarizes the complete project and has some footage of it in action.
