# Free cloud deployment (Render + Neon)

PrithivMart is a C++17 Drogon application with a PostgreSQL database and a same-origin HTML frontend.

## 1. Create a Neon PostgreSQL database

1. Create a project at https://console.neon.tech/.
2. Run the SQL schema required by `main.cc` in the Neon SQL Editor.
3. Copy the PostgreSQL connection string. Keep it private.

## 2. Deploy the backend on Render

1. Push the repository to GitHub.
2. In https://dashboard.render.com/, create **New → Web Service** and select this repository.
3. Select **Docker** as the runtime and the free instance type if offered.
4. Add the environment variable `PRITHIVMART_DATABASE_URL` with the Neon connection string. The connection string should include SSL mode, for example `sslmode=require`.
5. Add any required email/OTP environment variables only if those features are being used.
6. Deploy and inspect the build and runtime logs.

The server reads Render's `PORT` environment variable and listens on `0.0.0.0`. The frontend is served by the same Drogon service, so use the Render service URL rather than opening the HTML file directly.

## Important checks before public use

- Confirm the exact PostgreSQL schema and required tables/columns from `main.cc`; do not assume a schema from an older draft.
- Do not commit real passwords, database URLs, Gmail app passwords, or API tokens.
- Replace or remove sample credentials in `config.json` before deployment.
- Test registration, OTP delivery, login, products, cart, checkout, and orders using the deployed URL.
- Free hosting plans have usage limits and may sleep when idle. Check the current Render and Neon plan limits before relying on the service.
- This guide prepares the deployment path; a successful public deployment is not confirmed until Render builds and the live endpoints are tested.
