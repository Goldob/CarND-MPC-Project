#include <math.h>
#include <uWS/uWS.h>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include "Eigen-3.3/Eigen/Core"
#include "Eigen-3.3/Eigen/QR"
#include "helpers.h"
#include "json.hpp"
#include "MPC.h"

// for convenience
using nlohmann::json;
using std::string;
using std::vector;
using Eigen::VectorXd;

// For converting back and forth between radians and degrees.
constexpr double pi() { return M_PI; }
double deg2rad(double x) { return x * pi() / 180; }
double rad2deg(double x) { return x * 180 / pi(); }

double latency_dt = 0.0;
const double k = 0.1;

int main() {
  uWS::Hub h;

  // MPC is initialized here!
  MPC mpc;

  h.onMessage([&mpc](uWS::WebSocket<uWS::SERVER> ws, char *data, size_t length,
                     uWS::OpCode opCode) {
    // "42" at the start of the message means there's a websocket message event.
    // The 4 signifies a websocket message
    // The 2 signifies a websocket event
    string sdata = string(data).substr(0, length);
    std::cout << sdata << std::endl;
    if (sdata.size() > 2 && sdata[0] == '4' && sdata[1] == '2') {
	  auto start = std::chrono::system_clock::now();

      string s = hasData(sdata);
      if (s != "") {
        auto j = json::parse(s);
        string event = j[0].get<string>();
        if (event == "telemetry") {
          // j[1] is the data JSON object
          vector<double> ptsx = j[1]["ptsx"];
          vector<double> ptsy = j[1]["ptsy"];
          double px = j[1]["x"];
          double py = j[1]["y"];
          double psi = j[1]["psi"];
          double v = j[1]["speed"];
		  double delta = j[1]["steering_angle"];
		  double a = j[1]["throttle"];

		  // MPH to m/s
		  v = v * 0.44704;

		  // Add latency of 100ms
		  px = px + v * cos(psi) * latency_dt;
		  py = py + v * sin(psi) * latency_dt;
		  psi = psi - v * delta / Lf * latency_dt;
		  v = v + a * latency_dt;

          /**
           * Transform waypoints to the coordinate space of the vehicle
           */
          vector<double> local_ptsx;
          vector<double> local_ptsy;
          VectorXd v_local_ptsx(ptsx.size());
          VectorXd v_local_ptsy(ptsx.size());
          for (unsigned int i = 0; i < ptsx.size(); i++) {
            // Translation
            double ptx = ptsx[i] - px;
            double pty = ptsy[i] - py;

            // Rotation
            local_ptsx.push_back( ptx * cos(psi) + pty * sin(psi));
            local_ptsy.push_back(-ptx * sin(psi) + pty * cos(psi));

            // Add points to Eigen vector as well (for polyfit)
            v_local_ptsx(i) = local_ptsx[i];
            v_local_ptsy(i) = local_ptsy[i];
          }

          /**
           * Approximate waypoints with a 2nd degree polynomial
           */
          VectorXd coeffs = polyfit(v_local_ptsx, v_local_ptsy, 2);

          /**
           * Set up state variables
           */
          double cte  = polyeval(coeffs, 0) - py;
          double epsi = psi - atan(coeffs[1]);

		  // Add everything to the state vector
          VectorXd state(6);
          state << 0, 0, 0, v, cte, epsi;

          /**
           * Calculate steering angle and throttle using MPC.
           * Both are in between [-1, 1].
           */
          auto vars = mpc.Solve(state, coeffs);

          double steer_value = vars[0];
          double throttle_value = vars[1];

          json msgJson;
          // NOTE: Remember to divide by deg2rad(25) before you send the
          //   steering value back. Otherwise the values will be in between
          //   [-deg2rad(25), deg2rad(25] instead of [-1, 1].
          msgJson["steering_angle"] = - steer_value / deg2rad(25);
          msgJson["throttle"] = throttle_value;

          // Display the MPC predicted trajectory
          vector<double> mpc_x_vals;
          vector<double> mpc_y_vals;

          for (int i = 1; i < vars.size() / 2; i++) {
            mpc_x_vals.push_back(vars[2*i]);
            mpc_y_vals.push_back(vars[2*i + 1]);
          }

          msgJson["mpc_x"] = mpc_x_vals;
          msgJson["mpc_y"] = mpc_y_vals;

          // Display the waypoints/reference line
          vector<double> next_x_vals = local_ptsx;
          vector<double> next_y_vals = local_ptsy;

          msgJson["next_x"] = next_x_vals;
          msgJson["next_y"] = next_y_vals;


          auto msg = "42[\"steer\"," + msgJson.dump() + "]";
          std::cout << msg << std::endl;
          // Latency
          // The purpose is to mimic real driving conditions where
          //   the car does actuate the commands instantly.
          //
          // Feel free to play around with this value but should be to drive
          //   around the track with 100ms latency.
          //
          // NOTE: REMEMBER TO SET THIS TO 100 MILLISECONDS BEFORE SUBMITTING.
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
		  auto end = std::chrono::system_clock::now();
		  auto diff = std::chrono::duration<float, std::chrono::seconds::period>(end-start);
		  latency_dt = k * diff.count() + (1.0 - k) * latency_dt;
          ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
        }  // end "telemetry" if
      } else {
        // Manual driving
        std::string msg = "42[\"manual\",{}]";
        ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
      }
    }  // end websocket if
  }); // end h.onMessage

  h.onConnection([&h](uWS::WebSocket<uWS::SERVER> ws, uWS::HttpRequest req) {
    std::cout << "Connected!!!" << std::endl;
  });

  h.onDisconnection([&h](uWS::WebSocket<uWS::SERVER> ws, int code,
                         char *message, size_t length) {
    ws.close();
    std::cout << "Disconnected" << std::endl;
  });

  int port = 4567;
  if (h.listen(port)) {
    std::cout << "Listening to port " << port << std::endl;
  } else {
    std::cerr << "Failed to listen to port" << std::endl;
    return -1;
  }

  h.run();
}
