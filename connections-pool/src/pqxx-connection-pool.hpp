#ifndef _PQXX_CONNECTION_POOL_HPP
#define _PQXX_CONNECTION_POOL_HPP

#include <string>
#include <unordered_set>
#include <mutex>
#include <pqxx/pqxx>
#include <queue>

namespace cp {
	struct basic_transaction;

	struct connection_options {
		std::string dbname{};
		std::string user{};
		std::string password{};
		std::string hostaddr{};
		int16_t port = 5432;
	};

	struct connection_manager {
		connection_manager(std::unique_ptr<pqxx::connection>& connection) : connection(std::move(connection)){};
		connection_manager(const connection_manager&) = delete;
		connection_manager& operator=(const connection_manager&) = delete;

		void prepare(const std::string& name, const std::string& definition) {
			std::scoped_lock lock(prepares_mutex);
			if (prepares.contains(name))
				return;

			connection->prepare(name, definition);
			prepares.insert(name);
		}

		friend struct basic_connection;

	private:
		std::unordered_set<std::string> prepares{};
		std::mutex prepares_mutex{};
		std::unique_ptr<pqxx::connection> connection{};
	};

	struct connection_pool {
		connection_pool(const connection_options& options) {
			for (int i = 0; i < 8; ++i) {
				const auto connect_string = std::format("dbname = {} user = {} password = {} hostaddr = {} port = {}", options.dbname, options.user, options.password, options.hostaddr, options.port);

				auto connection = std::make_unique<pqxx::connection>(connect_string);
				auto manager = std::make_unique<connection_manager>(connection);
				connections.push(std::move(manager));
			}
		}

		std::unique_ptr<connection_manager> borrow_connection() {
			std::unique_lock lock(connections_mutex);
			connections_cond.wait(lock, [this]() { return !connections.empty(); });

			// if we have something here, we can borrow it from the queue
			auto manager = std::move(connections.front());
			connections.pop();
			return manager;
		}

		void return_connection(std::unique_ptr<connection_manager>& manager) {
			// return the borrowed connection
			{
				std::scoped_lock lock(connections_mutex);
				connections.push(std::move(manager));
			}

			// notify that we're done
			connections_cond.notify_one();
		}

	private:
		std::mutex connections_mutex{};
		std::condition_variable connections_cond{};
		std::queue<std::unique_ptr<connection_manager>> connections{};
	};

	struct basic_connection final {
		basic_connection(connection_pool& pool) : pool(pool) {
			manager = pool.borrow_connection();
		}

		~basic_connection() {
			pool.return_connection(manager);
		}

		pqxx::connection& get() const { return *manager->connection; }

		operator pqxx::connection&() { return get(); }
		operator const pqxx::connection&() const { return get(); }

		pqxx::connection* operator->() { return manager->connection.get(); }
		const pqxx::connection* operator->() const { return manager->connection.get(); }

		void prepare(std::string_view name, std::string_view definition) {
			manager->prepare(std::string(name), std::string(definition));
		}

		basic_connection(const basic_connection&) = delete;
		basic_connection& operator=(const basic_connection&) = delete;

	private:
		connection_pool& pool;
		std::unique_ptr<connection_manager> manager;
	};

	struct query_manager {
		query_manager(basic_transaction& transaction, std::string_view query_id) : transaction_view(transaction), query_id(query_id) {}

		template<typename... Args>
		pqxx::result exec_prepared(Args&&... args);

	private:
		std::string query_id{};
		basic_transaction& transaction_view;
	};

	struct query {
		query(std::string_view str) : str(str) {}

		const char* data() const {
			return str.data();
		}

		operator std::string() const {
			return { str.begin(), str.end() };
		}

		constexpr operator std::string_view() const {
			return { str.data(), str.size() };
		}

		template<typename... Args>
		pqxx::result operator()(Args&&... args) {
			return exec(std::forward<Args>(args)...);
		}

		template<typename... Args>
		pqxx::result exec(Args&&... args) {
			if (!manager.has_value())
				throw std::runtime_error("attempt to execute a query without connection with a transaction");

			return manager->exec_prepared(std::forward<Args>(args)...);
		}

		friend struct query_manager;
		friend struct basic_transaction;

	protected:
		std::string str;
		mutable std::optional<query_manager> manager{};
	};

	struct named_query : query {
		named_query(std::string_view name, std::string_view str) : query(str), name(name) {}

		friend struct query_manager;
		friend struct basic_transaction;

	protected:
		std::string name;
	};

	struct basic_transaction {
		void prepare_one(const query& q) {
			const auto query_id = std::format("{:X}", std::hash<std::string_view>()(q));
			connection.prepare(query_id, q);
			q.manager.emplace(*this, query_id);
		}

		void prepare_one(const named_query& q) {
			connection.prepare(q.name, q);
			q.manager.emplace(*this, q.name);
		}

		template<typename... Queries>
		void prepare(Queries&&... queries) {
			(prepare_one(std::forward<Queries>(queries)), ...);
		}

		template<typename... Queries>
		basic_transaction(connection_pool& pool, Queries&&... queries) : connection(pool), transaction(connection.get()) {
			prepare(std::forward<Queries>(queries)...);
		}

		basic_transaction(const basic_transaction&) = delete;
		basic_transaction& operator=(const basic_transaction&) = delete;

		pqxx::result exec(std::string_view q) { return transaction.exec(q); }
		void commit() { transaction.commit(); }
		void abort() { transaction.abort(); }

		pqxx::work& get() { return transaction; }
		operator pqxx::work&() { return get(); }

		friend struct query_manager;
		friend struct connection_pool;

	private:
		basic_connection connection;
		pqxx::work transaction;
	};

	template<typename... Args>
	pqxx::result query_manager::exec_prepared(Args&&... args) {
		return transaction_view.transaction.exec_prepared(query_id, std::forward<Args>(args)...);
	}

	template<typename... Queries>
	static basic_transaction tx(connection_pool& pool, Queries&&... queries) {
		return basic_transaction(pool, std::forward<Queries>(queries)...);
	}
} // namespace cp

#endif
