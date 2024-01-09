#include "pqxx-connection-pool.hpp"
#include <iostream>

#pragma comment(lib, "crypt32")
#pragma comment(lib, "libpq")
#pragma comment(lib, "ws2_32")
#pragma comment(lib, "wldap32")
#pragma comment(lib, "secur32")

static void insert_user(cp::connection_pool& pool, int rnd) {
	cp::named_query add_user("add_user", "INSERT INTO test_users (username, role) VALUES ($1, $2)");

	try {
		// Start a transaction
		auto tx = cp::tx(pool, add_user);

		// Execute the query
		add_user(std::format("{:X}", rnd), "user");

		// Commit the transaction
		tx.commit();
	} catch (std::exception& e) {
		std::cerr << "Error: " << e.what() << std::endl;
	}
}

static std::vector<std::pair<std::string, std::string>> read_users(cp::connection_pool& pool) {
	cp::named_query get_users("get_users", "SELECT username, role FROM test_users");
	std::vector<std::pair<std::string, std::string>> user_data;

	try {
		// Start a transaction
		auto tx = cp::tx(pool, get_users);

		// Execute the query
		const auto& r = get_users();

		// Iterate over the result set
		for (const auto& row: r) {
			std::string username = row["username"].as<std::string>();
			std::string role = row["role"].as<std::string>();

			// Add the username and role to the vector
			user_data.emplace_back(username, role);
		}
	} catch (std::exception& e) {
		std::cerr << "Error: " << e.what() << std::endl;
	}

	return user_data;
}

int main() {
	srand(time(0));

	cp::connection_options options{};
	options.dbname = "postgres";
	options.user = "postgres";
	options.password = "password";
	options.hostaddr = "127.0.0.1";

	cp::connection_pool pool(options);
	const auto old_size = read_users(pool).size();

	try {
		cp::query create_table("CREATE TABLE IF NOT EXISTS test_users ("
							   "id SERIAL PRIMARY KEY,"
							   "username TEXT,"
							   "role TEXT)");

		auto tx = cp::tx(pool, create_table);
		create_table();
		tx.commit();
	} catch (std::exception& e) {
		std::cout << e.what() << std::endl;
		return 1;
	}

	std::chrono::system_clock::time_point start;
	{
		std::vector<std::jthread> threads{};

		for (int i = 0; i < 1000; ++i)
			threads.emplace_back([&pool, rnd = rand()]() { insert_user(pool, rnd); });

		start = std::chrono::system_clock::now();
	}

	assert(read_users(pool).size() == old_size + 1000);

	const auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - start);
	std::cout << std::format("{}ms\n", diff.count());
	return 0;
}
