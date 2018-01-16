// Fergus Kelley
// CSC 4410 Embedded Systems

// Car Telemetry Project
// OBDLogNet
// This is a frontend for OBDLog, using Node.js

// All code by Fergus Kelley, except where otherwise stated

var app = require('express')();
var http = require('http').Server(app);
var io = require('socket.io')(http);
const exec = require('child_process').exec;

var port = 3000;
// var serialPort = "/dev/pts/1";
var serialPort = '/dev/ttyUSB0';
var OBDLog = '../OBDLog/OBDLog';

// Setup the UART
console.log('# Setting up UART');
exec('./' + OBDLog + ' -s -d 1 -i 2000 -c ATRV -p ' + serialPort);

// Send the HTML
app.get('/', function(req, res){
	res.sendFile(__dirname + '/index.html');
});

// When a user connects, register the sockets
io.on('connection', function(socket){

	// This is called when the user hits a log button
	socket.on('log', function(msg){
		console.log('# Logging: ' + msg);

		var obdCommand;
		var duration = 0; // Log until stopped
		var interval = 500; // in milliseconds

		switch (msg) {
			case 'rpm':
				obdCommand = '010C';
				break;
			case 'speed':
				obdCommand = '010D';
				break;
			case 'temp':
				obdCommand = '0105';
				break;
			default:
				console.log('Unknown command: ' + msg);
				return;
		}	

		var logCommand = './' + OBDLog + ' -d ' + duration + ' -i ' + interval + ' -p ' + serialPort + ' -c ' + obdCommand;
		var child = exec(logCommand);

		child.stdout.on('data', function(dataString) {

			// Strip whitespace from the output of OBDLog
			dataString = dataString.trim()
			console.log(dataString);
			io.emit('rawData', dataString);

			// If the first char is #, this is an informational message that does not need to be displayed
			if ( dataString.charAt(0) === '#' ) {
				return;
			}

			// Split the data string in an array on the ',' char
			// data[0] is the timestamp
			// data[1] is the command
			// data[2] and above are the returned data points from each ECU
			var data = dataString.split(',');
			var output = '';

			// Right now we only care about engine data, which is ECU 7E8
			// So if a different ECU is sending data, just ignore it
			// TODO Support data from different ECUs: Hybrid ECU is 7EA, Battery ECU is 7EB
			// Loop through the returned data points to find the engine data
			var engineData = '';
			for (var i = 2; i < data.length - 1; i++) {
				var ECU = data[i].substring(0, 3);
				if ( ECU !== '7E8') {
					engineData = data[i];
					break;
				}
			}

			// For each data point
			// 1. Determine the OBD command
			// 2. Extract the useful data bytes
			// 3. Parse those data bytes according the OBD specification
			// 4. Build the output string
			var obdCommand = data[1];
			var dataName = '';
			var dataValue = '';
			var dataUnit = '';
			if ( obdCommand == '010C') { // RPM

				var aBytes = engineData.substring(9,11);
				var bBytes = engineData.substring(11,13);

				var a = parseInt(aBytes, 16);
				var b = parseInt(bBytes, 16);

				var RPM = ((256 * a) + b) / 4;

				dataName = 'Engine RPM';
				dataValue = RPM;
				dataUnit = 'RPM';


			} else if ( obdCommand == '010D') { // Speed

				var aBytes = engineData.substring(9,11);

				var a = parseInt(aBytes, 16);

				var Speed = a;

				dataName = 'Vehicle Speed';
				dataValue = Speed;
				dataUnit = 'km/h';


			} else if ( obdCommand == '0105') { // Temp

				var aBytes = engineData.substring(9,11);

				var a = parseInt(aBytes, 16);

				var Temp = a - 40;

				dataName = 'Engine Coolant Temperature';
				dataValue = Temp;
				dataUnit = '°C';

			}

			// Build the output string and send it over the socket
			output += '<div class="dataName">' + dataName + ':</div><br/><div class="dataValue">' + dataValue + '</div><br/><div class="dataUnit">' + dataUnit + '</div>';
			io.emit('parsedData', output);
		});
	});

	// This is called when the user hits the stop button
	// It sends a single command to the UART and then reaches the duration and stops
	socket.on('stopLog', function(msg){
		console.log('# Stopping');
		exec('./' + OBDLog + ' -d 1 -i 2000 -c ATRV -p ' + serialPort);
	});
});

http.listen(port, function(){
	console.log('# Server available on port ' + port);
});