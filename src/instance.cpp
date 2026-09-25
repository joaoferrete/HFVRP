#include "instance.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

using namespace std;

namespace hfvrp {

double Instance::distance_bound() const {
    double total = 0.0;
    for (int i = 0; i <= num_customers; ++i) {
        for (int j = 0; j <= num_customers; ++j) {
            total += distance[i][j];
        }
    }
    return total;
}

double Instance::total_capacity() const {
    double s = 0.0;
    for (const auto& v : vehicles) s += v.capacity;
    return s;
}

double Instance::total_demand() const {
    double s = 0.0;
    for (int i = 1; i <= num_customers; ++i) s += demand[i];
    return s;
}

namespace {

void compute_distances(Instance& inst) {
    const int n = inst.num_customers;
    inst.distance.assign(n + 1, vector<double>(n + 1, 0.0));
    for (int i = 0; i <= n; ++i) {
        for (int j = 0; j <= n; ++j) {
            if (i == j) continue;
            const double dx = inst.coord_x[i] - inst.coord_x[j];
            const double dy = inst.coord_y[i] - inst.coord_y[j];
            inst.distance[i][j] = sqrt(dx * dx + dy * dy);
        }
    }
}

string trim(const string& s) {
    const auto a = s.find_first_not_of(" \t\r\n");
    if (a == string::npos) return "";
    const auto b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

} // namespace

Instance load_instance(const string& path) {
    ifstream in(path);
    if (!in) throw runtime_error("Cannot open instance file: " + path);

    Instance inst;
    string line, section;
    bool have_n = false, have_m = false;

    while (getline(in, line)) {
        const string t = trim(line);
        if (t.empty() || t[0] == '#') continue;

        const auto colon = t.find(':');
        if (colon != string::npos &&
            t.substr(0, colon).find(' ') == string::npos) {
            const string key = trim(t.substr(0, colon));
            const string val = trim(t.substr(colon + 1));
            if (key == "NAME") {
                inst.name = val;
            } else if (key == "CUSTOMERS" || key == "DIMENSION") {
                // DIMENSION conta o deposito; CUSTOMERS ja e o N.
                int v = stoi(val);
                inst.num_customers = (key == "DIMENSION") ? v - 1 : v;
                have_n = true;
            } else if (key == "VEHICLES") {
                inst.num_vehicles = stoi(val);
                have_m = true;
            }
            continue;
        }

        if (t == "NODE_COORD_SECTION" || t == "DEMAND_SECTION" ||
            t == "PRIORITY_SECTION" || t == "VEHICLE_SECTION" || t == "EOF") {
            section = t;
            if (section == "NODE_COORD_SECTION") {
                if (!have_n) throw runtime_error("CUSTOMERS/DIMENSION missing before coords");
                inst.coord_x.assign(inst.num_customers + 1, 0.0);
                inst.coord_y.assign(inst.num_customers + 1, 0.0);
            } else if (section == "DEMAND_SECTION") {
                inst.demand.assign(inst.num_customers + 1, 0.0);
            } else if (section == "PRIORITY_SECTION") {
                inst.priority.assign(inst.num_customers + 1, 0.0);
            } else if (section == "VEHICLE_SECTION") {
                if (!have_m) throw runtime_error("VEHICLES missing before vehicle section");
                inst.vehicles.assign(inst.num_vehicles, {});
            }
            continue;
        }

        istringstream iss(t);
        if (section == "NODE_COORD_SECTION") {
            int id; double x, y; iss >> id >> x >> y;
            inst.coord_x[id] = x; inst.coord_y[id] = y;
        } else if (section == "DEMAND_SECTION") {
            int id; double d; iss >> id >> d;
            inst.demand[id] = d;
        } else if (section == "PRIORITY_SECTION") {
            int id; double p; iss >> id >> p;
            inst.priority[id] = p;
        } else if (section == "VEHICLE_SECTION") {
            int id; double cap, fc, vc; iss >> id >> cap >> fc >> vc;
            inst.vehicles[id - 1] = {cap, fc, vc};
        }
    }

    if (inst.priority.empty()) inst.priority.assign(inst.num_customers + 1, 0.0);
    inst.demand[0] = 0.0;
    inst.priority[0] = 0.0;

    compute_distances(inst);
    return inst;
}

void save_instance(const Instance& inst, const string& path) {
    ofstream out(path);
    if (!out) throw runtime_error("Cannot write instance file: " + path);
    out << fixed << setprecision(6);
    out << "NAME: " << inst.name << '\n';
    out << "CUSTOMERS: " << inst.num_customers << '\n';
    out << "VEHICLES: " << inst.num_vehicles << '\n';

    out << "NODE_COORD_SECTION\n";
    for (int i = 0; i <= inst.num_customers; ++i)
        out << i << ' ' << inst.coord_x[i] << ' ' << inst.coord_y[i] << '\n';

    out << "DEMAND_SECTION\n";
    for (int i = 0; i <= inst.num_customers; ++i)
        out << i << ' ' << inst.demand[i] << '\n';

    out << "PRIORITY_SECTION\n";
    for (int i = 0; i <= inst.num_customers; ++i)
        out << i << ' ' << inst.priority[i] << '\n';

    out << "VEHICLE_SECTION\n";
    for (int k = 0; k < inst.num_vehicles; ++k) {
        const auto& v = inst.vehicles[k];
        out << (k + 1) << ' ' << v.capacity << ' ' << v.fixed_cost << ' ' << v.variable_cost << '\n';
    }
    out << "EOF\n";
}

} // namespace hfvrp
