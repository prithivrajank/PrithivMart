#include <drogon/drogon.h>
#include <libpq-fe.h>
#include <curl/curl.h>

#include <iostream>
#include <string>
#include <regex>

#include <chrono>
#include <random>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <fstream>

using namespace drogon;

class Database
{
private:
    static PGconn *connect()
    {
        const char *dbPassword = std::getenv("PRITHIVMART_DB_PASSWORD");

        if (!dbPassword)
        {
            std::cerr
                << "PRITHIVMART_DB_PASSWORD is not set."
                << std::endl;

            return nullptr;
        }

        std::string connectionString =
            "host=127.0.0.1 "
            "port=5432 "
            "dbname=prithivmart "
            "user=postgres "
            "password=" + std::string(dbPassword);

        return PQconnectdb(connectionString.c_str());
    }

    static std::string jsonMessage(
        const std::string &status,
        const std::string &message)
    {
        Json::Value data;
        data["status"] = status;
        data["message"] = message;

        Json::StreamWriterBuilder writer;
        return Json::writeString(writer, data);
    }

    static bool connectionOk(PGconn *conn)
    {
        return conn != nullptr && PQstatus(conn) == CONNECTION_OK;
    }

    static std::string generateOtp()
    {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> dist(100000, 999999);
        return std::to_string(dist(gen));
    }

    struct UploadStatus
    {
        std::string payload;
        size_t position = 0;
    };

    static size_t smtpReadCallback(
        char *ptr,
        size_t size,
        size_t nmemb,
        void *userdata)
    {
        auto *upload =
            static_cast<UploadStatus *>(userdata);

        const size_t capacity = size * nmemb;
        const size_t remaining =
            upload->payload.size() - upload->position;

        const size_t count =
            remaining < capacity ? remaining : capacity;

        if (count > 0)
        {
            std::memcpy(
                ptr,
                upload->payload.data() + upload->position,
                count
            );

            upload->position += count;
        }

        return count;
    }

    static size_t discardHttpResponse(
        char *ptr,
        size_t size,
        size_t nmemb,
        void *)
    {
        return size * nmemb;
    }

    static bool sendOtpEmail(
        const std::string &toEmail,
        const std::string &otp,
        std::string &errorMessage)
    {
        const char *senderEnv =
            std::getenv("PRITHIVMART_EMAIL");

        const char *appPasswordEnv =
            std::getenv("PRITHIVMART_EMAIL_APP_PASSWORD");

        if (!senderEnv || !appPasswordEnv)
        {
            errorMessage =
                "Gmail OTP is not configured. "
                "Set PRITHIVMART_EMAIL and "
                "PRITHIVMART_EMAIL_APP_PASSWORD.";

            return false;
        }

        const std::string sender = senderEnv;
        const std::string appPassword = appPasswordEnv;

        std::ostringstream mail;

        mail
            << "To: " << toEmail << "\r\n"
            << "From: PrithivMart <" << sender << ">\r\n"
            << "Subject: PrithivMart Verification OTP\r\n"
            << "MIME-Version: 1.0\r\n"
            << "Content-Type: text/plain; charset=UTF-8\r\n"
            << "\r\n"
            << "Your PrithivMart verification OTP is: "
            << otp << "\r\n\r\n"
            << "This OTP is valid for 5 minutes.\r\n"
            << "Do not share this OTP with anyone.\r\n";

        UploadStatus upload;
        upload.payload = mail.str();

        CURL *curl = curl_easy_init();

        if (!curl)
        {
            errorMessage =
                "Unable to initialize Gmail service.";

            return false;
        }

        struct curl_slist *recipients = nullptr;

        recipients =
            curl_slist_append(
                recipients,
                toEmail.c_str()
            );

        const std::string mailFrom =
            "<" + sender + ">";

        curl_easy_setopt(
            curl,
            CURLOPT_URL,
            "smtps://smtp.gmail.com:465"
        );

        curl_easy_setopt(
            curl,
            CURLOPT_USERNAME,
            sender.c_str()
        );

        curl_easy_setopt(
            curl,
            CURLOPT_PASSWORD,
            appPassword.c_str()
        );

        curl_easy_setopt(
            curl,
            CURLOPT_MAIL_FROM,
            mailFrom.c_str()
        );

        curl_easy_setopt(
            curl,
            CURLOPT_MAIL_RCPT,
            recipients
        );

        curl_easy_setopt(
            curl,
            CURLOPT_READFUNCTION,
            smtpReadCallback
        );

        curl_easy_setopt(
            curl,
            CURLOPT_READDATA,
            &upload
        );

        curl_easy_setopt(
            curl,
            CURLOPT_UPLOAD,
            1L
        );

        curl_easy_setopt(
            curl,
            CURLOPT_USE_SSL,
            CURLUSESSL_ALL
        );

        curl_easy_setopt(
            curl,
            CURLOPT_SSL_VERIFYPEER,
            1L
        );

        curl_easy_setopt(
            curl,
            CURLOPT_SSL_VERIFYHOST,
            2L
        );

        curl_easy_setopt(
            curl,
            CURLOPT_TIMEOUT,
            20L
        );

        CURLcode result =
            curl_easy_perform(curl);

        if (result != CURLE_OK)
        {
            errorMessage =
                std::string("OTP email failed: ") +
                curl_easy_strerror(result);
        }

        curl_slist_free_all(recipients);
        curl_easy_cleanup(curl);

        return result == CURLE_OK;
    }

    static bool sendLoginSms(
        const std::string &mobile,
        const std::string &name,
        std::string &statusMessage)
    {
        const char *sidEnv =
            std::getenv("TWILIO_ACCOUNT_SID");

        const char *tokenEnv =
            std::getenv("TWILIO_AUTH_TOKEN");

        const char *fromEnv =
            std::getenv("TWILIO_FROM_NUMBER");

        if (!sidEnv || !tokenEnv || !fromEnv)
        {
            statusMessage =
                "SMS provider is not configured.";

            return false;
        }

        std::string toNumber = mobile;

        if (std::regex_match(
                mobile,
                std::regex(R"(^[6-9][0-9]{9}$)")))
        {
            toNumber = "+91" + mobile;
        }

        CURL *curl = curl_easy_init();

        if (!curl)
        {
            statusMessage =
                "Unable to initialize SMS service.";

            return false;
        }

        const std::string sid = sidEnv;
        const std::string token = tokenEnv;
        const std::string fromNumber = fromEnv;

        const std::string url =
            "https://api.twilio.com/2010-04-01/Accounts/" +
            sid +
            "/Messages.json";

        const std::string bodyText =
            "PrithivMart login alert: "
            "A successful login was made to the account of " +
            name +
            ".";

        char *toEscaped =
            curl_easy_escape(
                curl,
                toNumber.c_str(),
                0
            );

        char *fromEscaped =
            curl_easy_escape(
                curl,
                fromNumber.c_str(),
                0
            );

        char *bodyEscaped =
            curl_easy_escape(
                curl,
                bodyText.c_str(),
                0
            );

        if (!toEscaped || !fromEscaped || !bodyEscaped)
        {
            if (toEscaped) curl_free(toEscaped);
            if (fromEscaped) curl_free(fromEscaped);
            if (bodyEscaped) curl_free(bodyEscaped);

            curl_easy_cleanup(curl);

            statusMessage =
                "Unable to prepare SMS request.";

            return false;
        }

        const std::string postFields =
            "To=" + std::string(toEscaped) +
            "&From=" + std::string(fromEscaped) +
            "&Body=" + std::string(bodyEscaped);

        curl_free(toEscaped);
        curl_free(fromEscaped);
        curl_free(bodyEscaped);

        curl_easy_setopt(
            curl,
            CURLOPT_URL,
            url.c_str()
        );

        curl_easy_setopt(
            curl,
            CURLOPT_USERNAME,
            sid.c_str()
        );

        curl_easy_setopt(
            curl,
            CURLOPT_PASSWORD,
            token.c_str()
        );

        curl_easy_setopt(
            curl,
            CURLOPT_POST,
            1L
        );

        curl_easy_setopt(
            curl,
            CURLOPT_POSTFIELDS,
            postFields.c_str()
        );

        curl_easy_setopt(
            curl,
            CURLOPT_WRITEFUNCTION,
            discardHttpResponse
        );

        curl_easy_setopt(
            curl,
            CURLOPT_TIMEOUT,
            20L
        );

        CURLcode result =
            curl_easy_perform(curl);

        long httpCode = 0;

        curl_easy_getinfo(
            curl,
            CURLINFO_RESPONSE_CODE,
            &httpCode
        );

        if (result != CURLE_OK)
        {
            statusMessage =
                std::string("SMS request failed: ") +
                curl_easy_strerror(result);
        }
        else if (httpCode < 200 || httpCode >= 300)
        {
            statusMessage =
                "SMS provider returned HTTP " +
                std::to_string(httpCode);
        }
        else
        {
            statusMessage = "sent";
        }

        curl_easy_cleanup(curl);

        return result == CURLE_OK &&
               httpCode >= 200 &&
               httpCode < 300;
    }

public:
    // =========================================================
    // DATABASE TEST
    // =========================================================
    static std::string testConnection()
    {
        PGconn *conn = connect();

        if (!connectionOk(conn))
        {
            std::string error = conn ? PQerrorMessage(conn) : "Database connection failed";
            if (conn) PQfinish(conn);
            return jsonMessage("error", error);
        }

        PQfinish(conn);
        return jsonMessage("success", "PostgreSQL Connected");
    }

    // =========================================================
    // REGISTER
    // =========================================================
    static std::string registerUser(
        const std::string &name,
        const std::string &email,
        const std::string &mobile,
        const std::string &password,
        const std::string &role)
    {
        if (name.empty() || email.empty() || mobile.empty() || password.empty())
            return jsonMessage("error", "All fields are required");

        if (role != "BUYER" && role != "SELLER")
            return jsonMessage("error", "Role must be BUYER or SELLER");

        const std::regex emailPattern(
            R"(^[A-Za-z0-9._%+-]+@gmail\.com$)",
            std::regex::icase
        );

        if (!std::regex_match(email, emailPattern))
            return jsonMessage(
                "error",
                "Enter a valid Gmail address ending with @gmail.com"
            );

        const std::regex mobilePattern(R"(^[6-9][0-9]{9}$)");

        if (!std::regex_match(mobile, mobilePattern))
            return jsonMessage(
                "error",
                "Enter a valid 10-digit Indian mobile number"
            );

        if (password.size() < 8 ||
            !std::regex_search(password, std::regex(R"([A-Z])")) ||
            !std::regex_search(password, std::regex(R"([a-z])")) ||
            !std::regex_search(password, std::regex(R"([0-9])")) ||
            !std::regex_search(password, std::regex(R"([^A-Za-z0-9])")))
        {
            return jsonMessage(
                "error",
                "Password needs 8+ characters, uppercase, lowercase, number and symbol"
            );
        }

        PGconn *conn = connect();

        if (!connectionOk(conn))
        {
            std::string error =
                conn ? PQerrorMessage(conn) : "Database connection failed";

            if (conn) PQfinish(conn);
            return jsonMessage("error", error);
        }

        // Check whether this email is already present.
        const char *emailValue[1] = { email.c_str() };

        PGresult *checkEmail = PQexecParams(
            conn,
            "SELECT id,is_verified "
            "FROM users "
            "WHERE email=$1",
            1,
            nullptr,
            emailValue,
            nullptr,
            nullptr,
            0
        );

        if (PQresultStatus(checkEmail) != PGRES_TUPLES_OK)
        {
            std::string error = PQerrorMessage(conn);
            PQclear(checkEmail);
            PQfinish(conn);
            return jsonMessage("error", error);
        }

        bool existingUnverified = false;
        int existingUserId = 0;

        if (PQntuples(checkEmail) > 0)
        {
            existingUserId = std::stoi(PQgetvalue(checkEmail, 0, 0));
            std::string verified = PQgetvalue(checkEmail, 0, 1);

            if (verified == "t")
            {
                PQclear(checkEmail);
                PQfinish(conn);
                return jsonMessage("error", "Email already registered");
            }

            existingUnverified = true;
        }

        PQclear(checkEmail);

        // Do not allow the same mobile number on another account.
        const char *mobileValue[2] = {
            mobile.c_str(),
            email.c_str()
        };

        PGresult *checkMobile = PQexecParams(
            conn,
            "SELECT id FROM users "
            "WHERE mobile=$1 AND email<>$2",
            2,
            nullptr,
            mobileValue,
            nullptr,
            nullptr,
            0
        );

        if (PQresultStatus(checkMobile) != PGRES_TUPLES_OK)
        {
            std::string error = PQerrorMessage(conn);
            PQclear(checkMobile);
            PQfinish(conn);
            return jsonMessage("error", error);
        }

        if (PQntuples(checkMobile) > 0)
        {
            PQclear(checkMobile);
            PQfinish(conn);
            return jsonMessage(
                "error",
                "Mobile number already registered"
            );
        }

        PQclear(checkMobile);

        const std::string otp = generateOtp();

        PGresult *result = nullptr;

        if (existingUnverified)
        {
            std::string id = std::to_string(existingUserId);

            const char *values[7] = {
                name.c_str(),
                mobile.c_str(),
                password.c_str(),
                role.c_str(),
                otp.c_str(),
                email.c_str(),
                id.c_str()
            };

            result = PQexecParams(
                conn,
                "UPDATE users SET "
                "name=$1,"
                "mobile=$2,"
                "password_hash=crypt($3,gen_salt('bf')),"
                "role=$4,"
                "otp_hash=crypt($5,gen_salt('bf')),"
                "otp_expires_at=CURRENT_TIMESTAMP + INTERVAL '5 minutes',"
                "otp_attempts=0 "
                "WHERE email=$6 AND id=$7 "
                "RETURNING id",
                7,
                nullptr,
                values,
                nullptr,
                nullptr,
                0
            );
        }
        else
        {
            const char *values[6] = {
                name.c_str(),
                email.c_str(),
                mobile.c_str(),
                password.c_str(),
                role.c_str(),
                otp.c_str()
            };

            result = PQexecParams(
                conn,
                "INSERT INTO users "
                "(name,email,mobile,password_hash,role,"
                "is_verified,otp_hash,otp_expires_at,otp_attempts) "
                "VALUES "
                "($1,$2,$3,crypt($4,gen_salt('bf')),$5,"
                "FALSE,crypt($6,gen_salt('bf')),"
                "CURRENT_TIMESTAMP + INTERVAL '5 minutes',0) "
                "RETURNING id",
                6,
                nullptr,
                values,
                nullptr,
                nullptr,
                0
            );
        }

        if (PQresultStatus(result) != PGRES_TUPLES_OK)
        {
            std::string error = PQerrorMessage(conn);
            PQclear(result);
            PQfinish(conn);
            return jsonMessage("error", error);
        }

        int userId = std::stoi(PQgetvalue(result, 0, 0));

        PQclear(result);
        PQfinish(conn);

        std::string emailError;

        if (!sendOtpEmail(
                email,
                otp,
                emailError))
        {
            return jsonMessage(
                "error",
                emailError +
                " Registration is saved but is not verified. "
                "After configuring Gmail, use Resend OTP."
            );
        }

        Json::Value response;
        response["status"] = "success";
        response["message"] =
            "OTP sent to your Gmail. Verify it to activate your account.";
        response["verification_required"] = true;
        response["email"] = email;
        response["user_id"] = userId;

        Json::StreamWriterBuilder writer;
        return Json::writeString(writer, response);
    }

    // =========================================================
    // VERIFY REGISTRATION OTP
    // =========================================================
    static std::string verifyRegistrationOtp(
        const std::string &email,
        const std::string &otp)
    {
        if (email.empty() || otp.empty())
            return jsonMessage("error", "Email and OTP are required");

        if (!std::regex_match(otp, std::regex(R"(^[0-9]{6}$)")))
            return jsonMessage("error", "OTP must contain 6 digits");

        PGconn *conn = connect();

        if (!connectionOk(conn))
        {
            std::string error =
                conn ? PQerrorMessage(conn) : "Database connection failed";

            if (conn) PQfinish(conn);
            return jsonMessage("error", error);
        }

        const char *values[2] = {
            email.c_str(),
            otp.c_str()
        };

        PGresult *result = PQexecParams(
            conn,
            "SELECT id,is_verified,otp_attempts,"
            "(otp_expires_at >= CURRENT_TIMESTAMP) AS valid_time,"
            "(otp_hash=crypt($2,otp_hash)) AS valid_otp "
            "FROM users "
            "WHERE email=$1",
            2,
            nullptr,
            values,
            nullptr,
            nullptr,
            0
        );

        if (PQresultStatus(result) != PGRES_TUPLES_OK)
        {
            std::string error = PQerrorMessage(conn);
            PQclear(result);
            PQfinish(conn);
            return jsonMessage("error", error);
        }

        if (PQntuples(result) == 0)
        {
            PQclear(result);
            PQfinish(conn);
            return jsonMessage("error", "Registration not found");
        }

        int userId = std::stoi(PQgetvalue(result, 0, 0));
        bool verified = std::string(PQgetvalue(result, 0, 1)) == "t";
        int attempts = std::stoi(PQgetvalue(result, 0, 2));
        bool validTime = std::string(PQgetvalue(result, 0, 3)) == "t";
        bool validOtp = std::string(PQgetvalue(result, 0, 4)) == "t";

        PQclear(result);

        if (verified)
        {
            PQfinish(conn);
            return jsonMessage("success", "Account already verified");
        }

        if (attempts >= 5)
        {
            PQfinish(conn);
            return jsonMessage(
                "error",
                "Too many incorrect attempts. Request a new OTP."
            );
        }

        if (!validTime)
        {
            PQfinish(conn);
            return jsonMessage(
                "error",
                "OTP expired. Request a new OTP."
            );
        }

        if (!validOtp)
        {
            const char *attemptValue[1] = { email.c_str() };

            PGresult *inc = PQexecParams(
                conn,
                "UPDATE users "
                "SET otp_attempts=otp_attempts+1 "
                "WHERE email=$1",
                1,
                nullptr,
                attemptValue,
                nullptr,
                nullptr,
                0
            );

            PQclear(inc);
            PQfinish(conn);

            return jsonMessage("error", "Incorrect OTP");
        }

        std::string id = std::to_string(userId);
        const char *verifyValues[1] = { id.c_str() };

        PGresult *verifyResult = PQexecParams(
            conn,
            "UPDATE users SET "
            "is_verified=TRUE,"
            "otp_hash=NULL,"
            "otp_expires_at=NULL,"
            "otp_attempts=0 "
            "WHERE id=$1",
            1,
            nullptr,
            verifyValues,
            nullptr,
            nullptr,
            0
        );

        if (PQresultStatus(verifyResult) != PGRES_COMMAND_OK)
        {
            std::string error = PQerrorMessage(conn);
            PQclear(verifyResult);
            PQfinish(conn);
            return jsonMessage("error", error);
        }

        PQclear(verifyResult);
        PQfinish(conn);

        return jsonMessage(
            "success",
            "OTP verified. Account activated successfully."
        );
    }

    // =========================================================
    // RESEND REGISTRATION OTP
    // =========================================================
    static std::string resendRegistrationOtp(
        const std::string &email)
    {
        if (email.empty())
            return jsonMessage("error", "Email is required");

        PGconn *conn = connect();

        if (!connectionOk(conn))
        {
            std::string error =
                conn ? PQerrorMessage(conn) : "Database connection failed";

            if (conn) PQfinish(conn);
            return jsonMessage("error", error);
        }

        const char *emailValue[1] = { email.c_str() };

        PGresult *userResult = PQexecParams(
            conn,
            "SELECT mobile,is_verified "
            "FROM users WHERE email=$1",
            1,
            nullptr,
            emailValue,
            nullptr,
            nullptr,
            0
        );

        if (PQresultStatus(userResult) != PGRES_TUPLES_OK ||
            PQntuples(userResult) == 0)
        {
            if (userResult) PQclear(userResult);
            PQfinish(conn);
            return jsonMessage("error", "Registration not found");
        }

        std::string mobile = PQgetvalue(userResult, 0, 0);
        bool verified = std::string(PQgetvalue(userResult, 0, 1)) == "t";

        PQclear(userResult);

        if (verified)
        {
            PQfinish(conn);
            return jsonMessage("error", "Account is already verified");
        }

        const std::string otp = generateOtp();

        const char *values[2] = {
            otp.c_str(),
            email.c_str()
        };

        PGresult *result = PQexecParams(
            conn,
            "UPDATE users SET "
            "otp_hash=crypt($1,gen_salt('bf')),"
            "otp_expires_at=CURRENT_TIMESTAMP + INTERVAL '5 minutes',"
            "otp_attempts=0 "
            "WHERE email=$2",
            2,
            nullptr,
            values,
            nullptr,
            nullptr,
            0
        );

        if (PQresultStatus(result) != PGRES_COMMAND_OK)
        {
            std::string error = PQerrorMessage(conn);
            PQclear(result);
            PQfinish(conn);
            return jsonMessage("error", error);
        }

        PQclear(result);
        PQfinish(conn);

        std::string emailError;

        if (!sendOtpEmail(
                email,
                otp,
                emailError))
        {
            return jsonMessage(
                "error",
                emailError
            );
        }

        return jsonMessage(
            "success",
            "A new OTP was sent to your Gmail."
        );
    }

    // =========================================================
    // LOGIN ALERT SMS
    // =========================================================
    static bool sendLoginAlertSms(
        const std::string &mobile,
        const std::string &name,
        std::string &statusMessage)
    {
        return sendLoginSms(
            mobile,
            name,
            statusMessage
        );
    }

    // =========================================================
    // AUTHENTICATE USER
    // =========================================================
    static bool authenticateUser(
        const std::string &email,
        const std::string &password,
        int &userId,
        std::string &name,
        std::string &role,
        std::string &mobile)
    {
        PGconn *conn = connect();

        if (!connectionOk(conn))
        {
            if (conn) PQfinish(conn);
            return false;
        }

        const char *values[2] = {
            email.c_str(),
            password.c_str()
        };

        PGresult *result = PQexecParams(
            conn,
            "SELECT id,name,role,COALESCE(mobile,'') "
            "FROM users "
            "WHERE email=$1 "
            "AND is_verified=TRUE "
            "AND password_hash=crypt($2,password_hash)",
            2,
            nullptr,
            values,
            nullptr,
            nullptr,
            0
        );

        if (PQresultStatus(result) != PGRES_TUPLES_OK ||
            PQntuples(result) == 0)
        {
            PQclear(result);
            PQfinish(conn);
            return false;
        }

        userId = std::stoi(PQgetvalue(result, 0, 0));
        name = PQgetvalue(result, 0, 1);
        role = PQgetvalue(result, 0, 2);
        mobile = PQgetvalue(result, 0, 3);

        PQclear(result);
        PQfinish(conn);

        return true;
    }

    // =========================================================
    // PUBLIC PRODUCTS
    // =========================================================
    static std::string getProducts()
    {
        PGconn *conn = connect();

        if (!connectionOk(conn))
        {
            std::string error = conn ? PQerrorMessage(conn) : "Database connection failed";
            if (conn) PQfinish(conn);
            return jsonMessage("error", error);
        }

        PGresult *result = PQexec(
            conn,
            "SELECT "
            "p.id,p.seller_id,u.name,p.name,p.description,"
            "p.category,p.price,p.stock,p.image_url "
            "FROM products p "
            "JOIN users u ON p.seller_id=u.id "
            "ORDER BY p.id"
        );

        if (PQresultStatus(result) != PGRES_TUPLES_OK)
        {
            std::string error = PQerrorMessage(conn);
            PQclear(result);
            PQfinish(conn);
            return jsonMessage("error", error);
        }

        Json::Value products(Json::arrayValue);

        for (int i = 0; i < PQntuples(result); i++)
        {
            Json::Value p;
            p["id"] = std::stoi(PQgetvalue(result, i, 0));
            p["seller_id"] = std::stoi(PQgetvalue(result, i, 1));
            p["seller"] = PQgetvalue(result, i, 2);
            p["name"] = PQgetvalue(result, i, 3);
            p["description"] = PQgetvalue(result, i, 4);
            p["category"] = PQgetvalue(result, i, 5);
            p["price"] = std::stod(PQgetvalue(result, i, 6));
            p["stock"] = std::stoi(PQgetvalue(result, i, 7));
            p["image_url"] = PQgetvalue(result, i, 8);

            products.append(p);
        }

        PQclear(result);
        PQfinish(conn);

        Json::StreamWriterBuilder writer;
        return Json::writeString(writer, products);
    }

    // =========================================================
    // SELLER ADD PRODUCT
    // =========================================================
    static std::string addProduct(
        int sellerId,
        const std::string &name,
        const std::string &description,
        const std::string &category,
        double price,
        int stock,
        const std::string &imageUrl)
    {
        if (sellerId <= 0 || name.empty() || price < 0 || stock < 0)
            return jsonMessage("error", "Invalid product data");

        PGconn *conn = connect();

        if (!connectionOk(conn))
        {
            std::string error = conn ? PQerrorMessage(conn) : "Database connection failed";
            if (conn) PQfinish(conn);
            return jsonMessage("error", error);
        }

        std::string seller = std::to_string(sellerId);
        std::string priceText = std::to_string(price);
        std::string stockText = std::to_string(stock);

        const char *values[7] = {
            seller.c_str(),
            name.c_str(),
            description.c_str(),
            category.c_str(),
            priceText.c_str(),
            stockText.c_str(),
            imageUrl.c_str()
        };

        PGresult *result = PQexecParams(
            conn,
            "INSERT INTO products "
            "(seller_id,name,description,category,price,stock,image_url) "
            "VALUES ($1,$2,$3,$4,$5,$6,$7) "
            "RETURNING id",
            7,
            nullptr,
            values,
            nullptr,
            nullptr,
            0
        );

        if (PQresultStatus(result) != PGRES_TUPLES_OK)
        {
            std::string error = PQerrorMessage(conn);
            PQclear(result);
            PQfinish(conn);
            return jsonMessage("error", error);
        }

        int productId = std::stoi(PQgetvalue(result, 0, 0));

        PQclear(result);
        PQfinish(conn);

        Json::Value response;
        response["status"] = "success";
        response["message"] = "Product Added Successfully";
        response["product_id"] = productId;

        Json::StreamWriterBuilder writer;
        return Json::writeString(writer, response);
    }

    // =========================================================
    // SELLER OWN PRODUCTS
    // =========================================================
    static std::string getSellerProducts(int sellerId)
    {
        PGconn *conn = connect();

        if (!connectionOk(conn))
        {
            std::string error = conn ? PQerrorMessage(conn) : "Database connection failed";
            if (conn) PQfinish(conn);
            return jsonMessage("error", error);
        }

        std::string seller = std::to_string(sellerId);
        const char *values[1] = { seller.c_str() };

        PGresult *result = PQexecParams(
            conn,
            "SELECT id,name,description,category,price,stock,image_url "
            "FROM products "
            "WHERE seller_id=$1 "
            "ORDER BY id",
            1,
            nullptr,
            values,
            nullptr,
            nullptr,
            0
        );

        if (PQresultStatus(result) != PGRES_TUPLES_OK)
        {
            std::string error = PQerrorMessage(conn);
            PQclear(result);
            PQfinish(conn);
            return jsonMessage("error", error);
        }

        Json::Value products(Json::arrayValue);

        for (int i = 0; i < PQntuples(result); i++)
        {
            Json::Value p;
            p["id"] = std::stoi(PQgetvalue(result, i, 0));
            p["name"] = PQgetvalue(result, i, 1);
            p["description"] = PQgetvalue(result, i, 2);
            p["category"] = PQgetvalue(result, i, 3);
            p["price"] = std::stod(PQgetvalue(result, i, 4));
            p["stock"] = std::stoi(PQgetvalue(result, i, 5));
            p["image_url"] = PQgetvalue(result, i, 6);

            products.append(p);
        }

        PQclear(result);
        PQfinish(conn);

        Json::StreamWriterBuilder writer;
        return Json::writeString(writer, products);
    }

    // =========================================================
    // SELLER UPDATE OWN PRODUCT
    // =========================================================
    static std::string updateProduct(
        int productId,
        int sellerId,
        const std::string &name,
        const std::string &description,
        const std::string &category,
        double price,
        int stock,
        const std::string &imageUrl)
    {
        if (name.empty() || price < 0 || stock < 0)
            return jsonMessage("error", "Invalid product data");

        PGconn *conn = connect();

        if (!connectionOk(conn))
        {
            std::string error = conn ? PQerrorMessage(conn) : "Database connection failed";
            if (conn) PQfinish(conn);
            return jsonMessage("error", error);
        }

        std::string product = std::to_string(productId);
        std::string seller = std::to_string(sellerId);
        std::string priceText = std::to_string(price);
        std::string stockText = std::to_string(stock);

        const char *values[8] = {
            name.c_str(),
            description.c_str(),
            category.c_str(),
            priceText.c_str(),
            stockText.c_str(),
            imageUrl.c_str(),
            product.c_str(),
            seller.c_str()
        };

        PGresult *result = PQexecParams(
            conn,
            "UPDATE products SET "
            "name=$1,description=$2,category=$3,price=$4,stock=$5,image_url=$6 "
            "WHERE id=$7 AND seller_id=$8",
            8,
            nullptr,
            values,
            nullptr,
            nullptr,
            0
        );

        if (PQresultStatus(result) != PGRES_COMMAND_OK)
        {
            std::string error = PQerrorMessage(conn);
            PQclear(result);
            PQfinish(conn);
            return jsonMessage("error", error);
        }

        int changed = std::stoi(PQcmdTuples(result));

        PQclear(result);
        PQfinish(conn);

        if (changed == 0)
            return jsonMessage("error", "Product not found or access denied");

        return jsonMessage("success", "Product Updated Successfully");
    }

    // =========================================================
    // SELLER DELETE OWN PRODUCT
    // =========================================================
    static std::string deleteProduct(int productId, int sellerId)
    {
        PGconn *conn = connect();

        if (!connectionOk(conn))
        {
            std::string error = conn ? PQerrorMessage(conn) : "Database connection failed";
            if (conn) PQfinish(conn);
            return jsonMessage("error", error);
        }

        std::string product = std::to_string(productId);
        std::string seller = std::to_string(sellerId);

        const char *values[2] = {
            product.c_str(),
            seller.c_str()
        };

        PGresult *result = PQexecParams(
            conn,
            "DELETE FROM products "
            "WHERE id=$1 AND seller_id=$2",
            2,
            nullptr,
            values,
            nullptr,
            nullptr,
            0
        );

        if (PQresultStatus(result) != PGRES_COMMAND_OK)
        {
            std::string error = PQerrorMessage(conn);
            PQclear(result);
            PQfinish(conn);
            return jsonMessage("error", error);
        }

        int changed = std::stoi(PQcmdTuples(result));

        PQclear(result);
        PQfinish(conn);

        if (changed == 0)
            return jsonMessage("error", "Product not found or access denied");

        return jsonMessage("success", "Product Deleted Successfully");
    }

    // =========================================================
    // BUYER ADD TO CART
    // =========================================================
    static std::string addToCart(
        int buyerId,
        int productId,
        int quantity)
    {
        if (quantity <= 0)
            return jsonMessage("error", "Quantity must be greater than 0");

        PGconn *conn = connect();

        if (!connectionOk(conn))
        {
            std::string error = conn ? PQerrorMessage(conn) : "Database connection failed";
            if (conn) PQfinish(conn);
            return jsonMessage("error", error);
        }

        std::string buyer = std::to_string(buyerId);
        std::string product = std::to_string(productId);
        std::string qty = std::to_string(quantity);

        const char *values[3] = {
            buyer.c_str(),
            product.c_str(),
            qty.c_str()
        };

        PGresult *result = PQexecParams(
            conn,
            "INSERT INTO cart_items (buyer_id,product_id,quantity) "
            "SELECT $1::int,$2::int,$3::int "
            "WHERE EXISTS ("
            "  SELECT 1 FROM products "
            "  WHERE id=$2::int AND stock >= $3::int"
            ") "
            "ON CONFLICT (buyer_id,product_id) "
            "DO UPDATE SET quantity=cart_items.quantity + EXCLUDED.quantity "
            "WHERE cart_items.quantity + EXCLUDED.quantity <= "
            "(SELECT stock FROM products WHERE id=$2::int) "
            "RETURNING id",
            3,
            nullptr,
            values,
            nullptr,
            nullptr,
            0
        );

        if (PQresultStatus(result) != PGRES_TUPLES_OK)
        {
            std::string error = PQerrorMessage(conn);
            PQclear(result);
            PQfinish(conn);
            return jsonMessage("error", error);
        }

        if (PQntuples(result) == 0)
        {
            PQclear(result);
            PQfinish(conn);
            return jsonMessage("error", "Product not found or not enough stock");
        }

        PQclear(result);
        PQfinish(conn);

        return jsonMessage("success", "Product Added To Cart");
    }

    // =========================================================
    // BUYER GET CART
    // =========================================================
    static std::string getCart(int buyerId)
    {
        PGconn *conn = connect();

        if (!connectionOk(conn))
        {
            std::string error = conn ? PQerrorMessage(conn) : "Database connection failed";
            if (conn) PQfinish(conn);
            return jsonMessage("error", error);
        }

        std::string buyer = std::to_string(buyerId);
        const char *values[1] = { buyer.c_str() };

        PGresult *result = PQexecParams(
            conn,
            "SELECT "
            "c.id,p.id,p.name,p.price,p.stock,c.quantity,"
            "(p.price*c.quantity) "
            "FROM cart_items c "
            "JOIN products p ON c.product_id=p.id "
            "WHERE c.buyer_id=$1 "
            "ORDER BY c.id",
            1,
            nullptr,
            values,
            nullptr,
            nullptr,
            0
        );

        if (PQresultStatus(result) != PGRES_TUPLES_OK)
        {
            std::string error = PQerrorMessage(conn);
            PQclear(result);
            PQfinish(conn);
            return jsonMessage("error", error);
        }

        Json::Value items(Json::arrayValue);
        double total = 0;

        for (int i = 0; i < PQntuples(result); i++)
        {
            Json::Value item;

            item["cart_id"] = std::stoi(PQgetvalue(result, i, 0));
            item["product_id"] = std::stoi(PQgetvalue(result, i, 1));
            item["name"] = PQgetvalue(result, i, 2);
            item["price"] = std::stod(PQgetvalue(result, i, 3));
            item["stock"] = std::stoi(PQgetvalue(result, i, 4));
            item["quantity"] = std::stoi(PQgetvalue(result, i, 5));

            double subtotal = std::stod(PQgetvalue(result, i, 6));
            item["subtotal"] = subtotal;

            total += subtotal;
            items.append(item);
        }

        PQclear(result);
        PQfinish(conn);

        Json::Value response;
        response["items"] = items;
        response["total"] = total;

        Json::StreamWriterBuilder writer;
        return Json::writeString(writer, response);
    }

    // =========================================================
    // BUYER UPDATE CART
    // =========================================================
    static std::string updateCart(
        int cartId,
        int buyerId,
        int quantity)
    {
        if (quantity <= 0)
            return jsonMessage("error", "Quantity must be greater than 0");

        PGconn *conn = connect();

        if (!connectionOk(conn))
        {
            std::string error = conn ? PQerrorMessage(conn) : "Database connection failed";
            if (conn) PQfinish(conn);
            return jsonMessage("error", error);
        }

        std::string cart = std::to_string(cartId);
        std::string buyer = std::to_string(buyerId);
        std::string qty = std::to_string(quantity);

        const char *values[3] = {
            qty.c_str(),
            cart.c_str(),
            buyer.c_str()
        };

        PGresult *result = PQexecParams(
            conn,
            "UPDATE cart_items c "
            "SET quantity=$1::int "
            "FROM products p "
            "WHERE c.id=$2::int "
            "AND c.buyer_id=$3::int "
            "AND c.product_id=p.id "
            "AND $1::int <= p.stock",
            3,
            nullptr,
            values,
            nullptr,
            nullptr,
            0
        );

        if (PQresultStatus(result) != PGRES_COMMAND_OK)
        {
            std::string error = PQerrorMessage(conn);
            PQclear(result);
            PQfinish(conn);
            return jsonMessage("error", error);
        }

        int changed = std::stoi(PQcmdTuples(result));

        PQclear(result);
        PQfinish(conn);

        if (changed == 0)
            return jsonMessage("error", "Invalid quantity or cart item");

        return jsonMessage("success", "Cart Updated");
    }

    // =========================================================
    // BUYER REMOVE CART ITEM
    // =========================================================
    static std::string removeCartItem(
        int cartId,
        int buyerId)
    {
        PGconn *conn = connect();

        if (!connectionOk(conn))
        {
            std::string error = conn ? PQerrorMessage(conn) : "Database connection failed";
            if (conn) PQfinish(conn);
            return jsonMessage("error", error);
        }

        std::string cart = std::to_string(cartId);
        std::string buyer = std::to_string(buyerId);

        const char *values[2] = {
            cart.c_str(),
            buyer.c_str()
        };

        PGresult *result = PQexecParams(
            conn,
            "DELETE FROM cart_items "
            "WHERE id=$1::int AND buyer_id=$2::int",
            2,
            nullptr,
            values,
            nullptr,
            nullptr,
            0
        );

        if (PQresultStatus(result) != PGRES_COMMAND_OK)
        {
            std::string error = PQerrorMessage(conn);
            PQclear(result);
            PQfinish(conn);
            return jsonMessage("error", error);
        }

        int changed = std::stoi(PQcmdTuples(result));

        PQclear(result);
        PQfinish(conn);

        if (changed == 0)
            return jsonMessage("error", "Cart item not found");

        return jsonMessage("success", "Item Removed From Cart");
    }

    // =========================================================
    // BUYER PLACE ORDER
    // =========================================================
    static std::string placeOrder(int buyerId)
    {
        PGconn *conn = connect();

        if (!connectionOk(conn))
        {
            std::string error = conn ? PQerrorMessage(conn) : "Database connection failed";
            if (conn) PQfinish(conn);
            return jsonMessage("error", error);
        }

        PGresult *begin = PQexec(conn, "BEGIN");
        if (PQresultStatus(begin) != PGRES_COMMAND_OK)
        {
            std::string error = PQerrorMessage(conn);
            PQclear(begin);
            PQfinish(conn);
            return jsonMessage("error", error);
        }
        PQclear(begin);

        std::string buyer = std::to_string(buyerId);
        const char *buyerValue[1] = { buyer.c_str() };

        PGresult *cart = PQexecParams(
            conn,
            "SELECT p.id,p.seller_id,p.price,p.stock,c.quantity "
            "FROM cart_items c "
            "JOIN products p ON c.product_id=p.id "
            "WHERE c.buyer_id=$1 "
            "FOR UPDATE OF p",
            1,
            nullptr,
            buyerValue,
            nullptr,
            nullptr,
            0
        );

        if (PQresultStatus(cart) != PGRES_TUPLES_OK)
        {
            std::string error = PQerrorMessage(conn);
            PQclear(cart);
            PGresult *r = PQexec(conn, "ROLLBACK");
            PQclear(r);
            PQfinish(conn);
            return jsonMessage("error", error);
        }

        int rows = PQntuples(cart);

        if (rows == 0)
        {
            PQclear(cart);
            PGresult *r = PQexec(conn, "ROLLBACK");
            PQclear(r);
            PQfinish(conn);
            return jsonMessage("error", "Cart is empty");
        }

        double total = 0;

        for (int i = 0; i < rows; i++)
        {
            double price = std::stod(PQgetvalue(cart, i, 2));
            int stock = std::stoi(PQgetvalue(cart, i, 3));
            int quantity = std::stoi(PQgetvalue(cart, i, 4));

            if (quantity <= 0 || quantity > stock)
            {
                PQclear(cart);
                PGresult *r = PQexec(conn, "ROLLBACK");
                PQclear(r);
                PQfinish(conn);
                return jsonMessage("error", "Not enough stock");
            }

            total += price * quantity;
        }

        std::string totalText = std::to_string(total);

        const char *orderValues[2] = {
            buyer.c_str(),
            totalText.c_str()
        };

        PGresult *orderResult = PQexecParams(
            conn,
            "INSERT INTO orders (buyer_id,total_amount,status) "
            "VALUES ($1,$2,'PLACED') "
            "RETURNING id",
            2,
            nullptr,
            orderValues,
            nullptr,
            nullptr,
            0
        );

        if (PQresultStatus(orderResult) != PGRES_TUPLES_OK)
        {
            std::string error = PQerrorMessage(conn);
            PQclear(orderResult);
            PQclear(cart);

            PGresult *r = PQexec(conn, "ROLLBACK");
            PQclear(r);

            PQfinish(conn);
            return jsonMessage("error", error);
        }

        int orderId = std::stoi(PQgetvalue(orderResult, 0, 0));
        PQclear(orderResult);

        for (int i = 0; i < rows; i++)
        {
            std::string productId = PQgetvalue(cart, i, 0);
            std::string sellerId = PQgetvalue(cart, i, 1);
            std::string price = PQgetvalue(cart, i, 2);
            std::string quantity = PQgetvalue(cart, i, 4);
            std::string order = std::to_string(orderId);

            const char *itemValues[5] = {
                order.c_str(),
                productId.c_str(),
                sellerId.c_str(),
                quantity.c_str(),
                price.c_str()
            };

            PGresult *item = PQexecParams(
                conn,
                "INSERT INTO order_items "
                "(order_id,product_id,seller_id,quantity,price) "
                "VALUES ($1,$2,$3,$4,$5)",
                5,
                nullptr,
                itemValues,
                nullptr,
                nullptr,
                0
            );

            if (PQresultStatus(item) != PGRES_COMMAND_OK)
            {
                std::string error = PQerrorMessage(conn);
                PQclear(item);
                PQclear(cart);

                PGresult *r = PQexec(conn, "ROLLBACK");
                PQclear(r);

                PQfinish(conn);
                return jsonMessage("error", error);
            }

            PQclear(item);

            const char *stockValues[2] = {
                quantity.c_str(),
                productId.c_str()
            };

            PGresult *stockResult = PQexecParams(
                conn,
                "UPDATE products "
                "SET stock=stock-$1::int "
                "WHERE id=$2::int "
                "AND stock >= $1::int",
                2,
                nullptr,
                stockValues,
                nullptr,
                nullptr,
                0
            );

            if (PQresultStatus(stockResult) != PGRES_COMMAND_OK ||
                std::stoi(PQcmdTuples(stockResult)) == 0)
            {
                std::string error = "Stock update failed";
                if (PQresultStatus(stockResult) != PGRES_COMMAND_OK)
                    error = PQerrorMessage(conn);

                PQclear(stockResult);
                PQclear(cart);

                PGresult *r = PQexec(conn, "ROLLBACK");
                PQclear(r);

                PQfinish(conn);
                return jsonMessage("error", error);
            }

            PQclear(stockResult);
        }

        PQclear(cart);

        PGresult *clear = PQexecParams(
            conn,
            "DELETE FROM cart_items "
            "WHERE buyer_id=$1",
            1,
            nullptr,
            buyerValue,
            nullptr,
            nullptr,
            0
        );

        if (PQresultStatus(clear) != PGRES_COMMAND_OK)
        {
            std::string error = PQerrorMessage(conn);
            PQclear(clear);

            PGresult *r = PQexec(conn, "ROLLBACK");
            PQclear(r);

            PQfinish(conn);
            return jsonMessage("error", error);
        }

        PQclear(clear);

        PGresult *commit = PQexec(conn, "COMMIT");

        if (PQresultStatus(commit) != PGRES_COMMAND_OK)
        {
            std::string error = PQerrorMessage(conn);
            PQclear(commit);
            PQfinish(conn);
            return jsonMessage("error", error);
        }

        PQclear(commit);
        PQfinish(conn);

        Json::Value response;
        response["status"] = "success";
        response["message"] = "Order Placed Successfully";
        response["order_id"] = orderId;
        response["total"] = total;

        Json::StreamWriterBuilder writer;
        return Json::writeString(writer, response);
    }

    // =========================================================
    // BUYER ORDERS
    // =========================================================
    static std::string getBuyerOrders(int buyerId)
    {
        PGconn *conn = connect();

        if (!connectionOk(conn))
        {
            std::string error = conn ? PQerrorMessage(conn) : "Database connection failed";
            if (conn) PQfinish(conn);
            return jsonMessage("error", error);
        }

        std::string buyer = std::to_string(buyerId);
        const char *values[1] = { buyer.c_str() };

        PGresult *result = PQexecParams(
            conn,
            "SELECT id,total_amount,status,created_at "
            "FROM orders "
            "WHERE buyer_id=$1 "
            "ORDER BY id DESC",
            1,
            nullptr,
            values,
            nullptr,
            nullptr,
            0
        );

        if (PQresultStatus(result) != PGRES_TUPLES_OK)
        {
            std::string error = PQerrorMessage(conn);
            PQclear(result);
            PQfinish(conn);
            return jsonMessage("error", error);
        }

        Json::Value orders(Json::arrayValue);

        for (int i = 0; i < PQntuples(result); i++)
        {
            Json::Value order;
            order["order_id"] = std::stoi(PQgetvalue(result, i, 0));
            order["total"] = std::stod(PQgetvalue(result, i, 1));
            order["status"] = PQgetvalue(result, i, 2);
            order["created_at"] = PQgetvalue(result, i, 3);

            orders.append(order);
        }

        PQclear(result);
        PQfinish(conn);

        Json::StreamWriterBuilder writer;
        return Json::writeString(writer, orders);
    }

    // =========================================================
    // SELLER ORDERS
    // =========================================================
    static std::string getSellerOrders(int sellerId)
    {
        PGconn *conn = connect();

        if (!connectionOk(conn))
        {
            std::string error = conn ? PQerrorMessage(conn) : "Database connection failed";
            if (conn) PQfinish(conn);
            return jsonMessage("error", error);
        }

        std::string seller = std::to_string(sellerId);
        const char *values[1] = { seller.c_str() };

        PGresult *result = PQexecParams(
            conn,
            "SELECT "
            "o.id,u.name,p.name,oi.quantity,oi.price,o.status,o.created_at "
            "FROM order_items oi "
            "JOIN orders o ON oi.order_id=o.id "
            "JOIN products p ON oi.product_id=p.id "
            "JOIN users u ON o.buyer_id=u.id "
            "WHERE oi.seller_id=$1 "
            "ORDER BY o.id DESC",
            1,
            nullptr,
            values,
            nullptr,
            nullptr,
            0
        );

        if (PQresultStatus(result) != PGRES_TUPLES_OK)
        {
            std::string error = PQerrorMessage(conn);
            PQclear(result);
            PQfinish(conn);
            return jsonMessage("error", error);
        }

        Json::Value orders(Json::arrayValue);

        for (int i = 0; i < PQntuples(result); i++)
        {
            Json::Value order;
            order["order_id"] = std::stoi(PQgetvalue(result, i, 0));
            order["buyer"] = PQgetvalue(result, i, 1);
            order["product"] = PQgetvalue(result, i, 2);
            order["quantity"] = std::stoi(PQgetvalue(result, i, 3));
            order["price"] = std::stod(PQgetvalue(result, i, 4));
            order["status"] = PQgetvalue(result, i, 5);
            order["created_at"] = PQgetvalue(result, i, 6);

            orders.append(order);
        }

        PQclear(result);
        PQfinish(conn);

        Json::StreamWriterBuilder writer;
        return Json::writeString(writer, orders);
    }

    // =========================================================
    // SELLER UPDATE ORDER STATUS
    // =========================================================
    static std::string updateOrderStatus(
        int orderId,
        int sellerId,
        const std::string &status)
    {
        if (status != "PROCESSING" &&
            status != "SHIPPED" &&
            status != "DELIVERED")
        {
            return jsonMessage("error", "Invalid status");
        }

        PGconn *conn = connect();

        if (!connectionOk(conn))
        {
            std::string error = conn ? PQerrorMessage(conn) : "Database connection failed";
            if (conn) PQfinish(conn);
            return jsonMessage("error", error);
        }

        std::string order = std::to_string(orderId);
        std::string seller = std::to_string(sellerId);

        const char *values[3] = {
            status.c_str(),
            order.c_str(),
            seller.c_str()
        };

        PGresult *result = PQexecParams(
            conn,
            "UPDATE orders "
            "SET status=$1 "
            "WHERE id=$2::int "
            "AND EXISTS ("
            "  SELECT 1 FROM order_items "
            "  WHERE order_id=$2::int AND seller_id=$3::int"
            ")",
            3,
            nullptr,
            values,
            nullptr,
            nullptr,
            0
        );

        if (PQresultStatus(result) != PGRES_COMMAND_OK)
        {
            std::string error = PQerrorMessage(conn);
            PQclear(result);
            PQfinish(conn);
            return jsonMessage("error", error);
        }

        int changed = std::stoi(PQcmdTuples(result));

        PQclear(result);
        PQfinish(conn);

        if (changed == 0)
            return jsonMessage("error", "Order not found or access denied");

        return jsonMessage("success", "Order Status Updated");
    }
};

// =============================================================
// AUTH HELPERS
// =============================================================
bool loggedIn(const HttpRequestPtr &req)
{
    auto session = req->session();
    return session && session->find("user_id");
}

bool hasRole(
    const HttpRequestPtr &req,
    const std::string &role)
{
    auto session = req->session();

    if (!session ||
        !session->find("user_id") ||
        !session->find("role"))
    {
        return false;
    }

    return session->get<std::string>("role") == role;
}

HttpResponsePtr jsonError(
    HttpStatusCode status,
    const std::string &message)
{
    Json::Value data;
    data["status"] = "error";
    data["message"] = message;

    auto resp = HttpResponse::newHttpJsonResponse(data);
    resp->setStatusCode(status);

    return resp;
}

// =============================================================
// MAIN
// =============================================================
int main()
{
    // Initialize libcurl once for Gmail/Twilio requests.
    curl_global_init(CURL_GLOBAL_DEFAULT);

    // Sessions are required for login, buyer, seller and admin routes.
    app().enableSession(3600);

    // ---------------------------------------------------------
    // FRONTEND
    // ---------------------------------------------------------
    app().registerHandler(
        "/",
        [](const HttpRequestPtr &,
           std::function<void(const HttpResponsePtr &)> &&callback)
        {
            auto resp = HttpResponse::newFileResponse("./prithivmart.html");
            callback(resp);
        },
        {Get}
    );

    // ---------------------------------------------------------
    // DATABASE TEST
    // ---------------------------------------------------------
    app().registerHandler(
        "/dbtest",
        [](const HttpRequestPtr &,
           std::function<void(const HttpResponsePtr &)> &&callback)
        {
            auto resp = HttpResponse::newHttpResponse();
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            resp->setBody(Database::testConnection());
            callback(resp);
        },
        {Get}
    );

    // ---------------------------------------------------------
    // REGISTER
    // ---------------------------------------------------------
    app().registerHandler(
        "/register",
        [](const HttpRequestPtr &req,
           std::function<void(const HttpResponsePtr &)> &&callback)
        {
            auto json = req->getJsonObject();

            if (!json)
            {
                callback(jsonError(k400BadRequest, "Invalid JSON"));
                return;
            }

            auto resp = HttpResponse::newHttpResponse();
            resp->setContentTypeCode(CT_APPLICATION_JSON);

            resp->setBody(
                Database::registerUser(
                    (*json)["name"].asString(),
                    (*json)["email"].asString(),
                    (*json)["mobile"].asString(),
                    (*json)["password"].asString(),
                    (*json)["role"].asString()
                )
            );

            callback(resp);
        },
        {Post}
    );

    // ---------------------------------------------------------
    // VERIFY REGISTRATION OTP
    // ---------------------------------------------------------
    app().registerHandler(
        "/verify-otp",
        [](const HttpRequestPtr &req,
           std::function<void(const HttpResponsePtr &)> &&callback)
        {
            auto json = req->getJsonObject();

            if (!json)
            {
                callback(jsonError(k400BadRequest, "Invalid JSON"));
                return;
            }

            auto resp = HttpResponse::newHttpResponse();
            resp->setContentTypeCode(CT_APPLICATION_JSON);

            resp->setBody(
                Database::verifyRegistrationOtp(
                    (*json)["email"].asString(),
                    (*json)["otp"].asString()
                )
            );

            callback(resp);
        },
        {Post}
    );

    // ---------------------------------------------------------
    // RESEND REGISTRATION OTP
    // ---------------------------------------------------------
    app().registerHandler(
        "/resend-otp",
        [](const HttpRequestPtr &req,
           std::function<void(const HttpResponsePtr &)> &&callback)
        {
            auto json = req->getJsonObject();

            if (!json)
            {
                callback(jsonError(k400BadRequest, "Invalid JSON"));
                return;
            }

            auto resp = HttpResponse::newHttpResponse();
            resp->setContentTypeCode(CT_APPLICATION_JSON);

            resp->setBody(
                Database::resendRegistrationOtp(
                    (*json)["email"].asString()
                )
            );

            callback(resp);
        },
        {Post}
    );

    // ---------------------------------------------------------
    // LOGIN
    // ---------------------------------------------------------
    app().registerHandler(
        "/login",
        [](const HttpRequestPtr &req,
           std::function<void(const HttpResponsePtr &)> &&callback)
        {
            auto json = req->getJsonObject();

            if (!json)
            {
                callback(jsonError(k400BadRequest, "Invalid JSON"));
                return;
            }

            std::string email = (*json)["email"].asString();
            std::string password = (*json)["password"].asString();

            if (email.empty() || password.empty())
            {
                callback(jsonError(
                    k400BadRequest,
                    "Email and password required"
                ));
                return;
            }

            int userId = 0;
            std::string name;
            std::string role;
            std::string mobile;

            if (!Database::authenticateUser(
                    email,
                    password,
                    userId,
                    name,
                    role,
                    mobile))
            {
                callback(jsonError(
                    k401Unauthorized,
                    "Invalid email or password"
                ));
                return;
            }

            auto session = req->session();

            session->clear();
            session->changeSessionIdToClient();

            session->insert("user_id", userId);
            session->insert("name", name);
            session->insert("role", role);

            std::string smsStatus;

            bool smsSent =
                Database::sendLoginAlertSms(
                    mobile,
                    name,
                    smsStatus
                );

            Json::Value response;
            response["status"] = "success";
            response["message"] = "Login Successful";
            response["id"] = userId;
            response["name"] = name;
            response["role"] = role;
            response["mobile"] = mobile;
            response["login_alert_sms"] =
                smsSent ? "sent" : "not_sent";

            if (!smsSent)
            {
                response["login_alert_info"] =
                    smsStatus;
            }

            callback(HttpResponse::newHttpJsonResponse(response));
        },
        {Post}
    );

    // ---------------------------------------------------------
    // CURRENT USER
    // ---------------------------------------------------------
    app().registerHandler(
        "/me",
        [](const HttpRequestPtr &req,
           std::function<void(const HttpResponsePtr &)> &&callback)
        {
            if (!loggedIn(req))
            {
                callback(jsonError(k401Unauthorized, "Not logged in"));
                return;
            }

            auto session = req->session();

            Json::Value user;
            user["status"] = "success";
            user["id"] = session->get<int>("user_id");
            user["name"] = session->get<std::string>("name");
            user["role"] = session->get<std::string>("role");

            callback(HttpResponse::newHttpJsonResponse(user));
        },
        {Get}
    );

    // ---------------------------------------------------------
    // LOGOUT
    // ---------------------------------------------------------
    app().registerHandler(
        "/logout",
        [](const HttpRequestPtr &req,
           std::function<void(const HttpResponsePtr &)> &&callback)
        {
            auto session = req->session();

            if (session)
            {
                session->clear();
                session->changeSessionIdToClient();
            }

            Json::Value data;
            data["status"] = "success";
            data["message"] = "Logged Out";

            callback(HttpResponse::newHttpJsonResponse(data));
        },
        {Post}
    );

    // ---------------------------------------------------------
    // LOGGED-IN PRODUCT BROWSING
    // ---------------------------------------------------------
    app().registerHandler(
        "/products",
        [](const HttpRequestPtr &req,
           std::function<void(const HttpResponsePtr &)> &&callback)
        {
            if (!loggedIn(req))
            {
                callback(jsonError(
                    k401Unauthorized,
                    "Login required to view products"
                ));
                return;
            }

            auto resp = HttpResponse::newHttpResponse();
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            resp->setBody(Database::getProducts());
            callback(resp);
        },
        {Get}
    );

    // ---------------------------------------------------------
    // SELLER ADD PRODUCT
    // ---------------------------------------------------------
    app().registerHandler(
        "/products",
        [](const HttpRequestPtr &req,
           std::function<void(const HttpResponsePtr &)> &&callback)
        {
            if (!hasRole(req, "SELLER"))
            {
                callback(jsonError(k403Forbidden, "Seller access only"));
                return;
            }

            auto json = req->getJsonObject();

            if (!json)
            {
                callback(jsonError(k400BadRequest, "Invalid JSON"));
                return;
            }

            int sellerId = req->session()->get<int>("user_id");

            auto resp = HttpResponse::newHttpResponse();
            resp->setContentTypeCode(CT_APPLICATION_JSON);

            resp->setBody(
                Database::addProduct(
                    sellerId,
                    (*json)["name"].asString(),
                    (*json)["description"].asString(),
                    (*json)["category"].asString(),
                    (*json)["price"].asDouble(),
                    (*json)["stock"].asInt(),
                    (*json)["image_url"].asString()
                )
            );

            callback(resp);
        },
        {Post}
    );

    // ---------------------------------------------------------
    // SELLER OWN PRODUCTS
    // ---------------------------------------------------------
    app().registerHandler(
        "/seller/products",
        [](const HttpRequestPtr &req,
           std::function<void(const HttpResponsePtr &)> &&callback)
        {
            if (!hasRole(req, "SELLER"))
            {
                callback(jsonError(k403Forbidden, "Seller access only"));
                return;
            }

            int sellerId = req->session()->get<int>("user_id");

            auto resp = HttpResponse::newHttpResponse();
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            resp->setBody(Database::getSellerProducts(sellerId));

            callback(resp);
        },
        {Get}
    );

    // ---------------------------------------------------------
    // SELLER UPDATE PRODUCT
    // ---------------------------------------------------------
    app().registerHandler(
        "/products/{1}",
        [](const HttpRequestPtr &req,
           std::function<void(const HttpResponsePtr &)> &&callback,
           int productId)
        {
            if (!hasRole(req, "SELLER"))
            {
                callback(jsonError(k403Forbidden, "Seller access only"));
                return;
            }

            auto json = req->getJsonObject();

            if (!json)
            {
                callback(jsonError(k400BadRequest, "Invalid JSON"));
                return;
            }

            int sellerId = req->session()->get<int>("user_id");

            auto resp = HttpResponse::newHttpResponse();
            resp->setContentTypeCode(CT_APPLICATION_JSON);

            resp->setBody(
                Database::updateProduct(
                    productId,
                    sellerId,
                    (*json)["name"].asString(),
                    (*json)["description"].asString(),
                    (*json)["category"].asString(),
                    (*json)["price"].asDouble(),
                    (*json)["stock"].asInt(),
                    (*json)["image_url"].asString()
                )
            );

            callback(resp);
        },
        {Put}
    );

    // ---------------------------------------------------------
    // SELLER DELETE PRODUCT
    // ---------------------------------------------------------
    app().registerHandler(
        "/products/{1}",
        [](const HttpRequestPtr &req,
           std::function<void(const HttpResponsePtr &)> &&callback,
           int productId)
        {
            if (!hasRole(req, "SELLER"))
            {
                callback(jsonError(k403Forbidden, "Seller access only"));
                return;
            }

            int sellerId = req->session()->get<int>("user_id");

            auto resp = HttpResponse::newHttpResponse();
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            resp->setBody(Database::deleteProduct(productId, sellerId));

            callback(resp);
        },
        {Delete}
    );

    // ---------------------------------------------------------
    // BUYER ADD CART
    // ---------------------------------------------------------
    app().registerHandler(
        "/cart",
        [](const HttpRequestPtr &req,
           std::function<void(const HttpResponsePtr &)> &&callback)
        {
            if (!hasRole(req, "BUYER"))
            {
                callback(jsonError(k403Forbidden, "Buyer access only"));
                return;
            }

            auto json = req->getJsonObject();

            if (!json)
            {
                callback(jsonError(k400BadRequest, "Invalid JSON"));
                return;
            }

            int buyerId = req->session()->get<int>("user_id");

            auto resp = HttpResponse::newHttpResponse();
            resp->setContentTypeCode(CT_APPLICATION_JSON);

            resp->setBody(
                Database::addToCart(
                    buyerId,
                    (*json)["product_id"].asInt(),
                    (*json)["quantity"].asInt()
                )
            );

            callback(resp);
        },
        {Post}
    );

    // ---------------------------------------------------------
    // BUYER VIEW CART
    // ---------------------------------------------------------
    app().registerHandler(
        "/cart",
        [](const HttpRequestPtr &req,
           std::function<void(const HttpResponsePtr &)> &&callback)
        {
            if (!hasRole(req, "BUYER"))
            {
                callback(jsonError(k403Forbidden, "Buyer access only"));
                return;
            }

            int buyerId = req->session()->get<int>("user_id");

            auto resp = HttpResponse::newHttpResponse();
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            resp->setBody(Database::getCart(buyerId));

            callback(resp);
        },
        {Get}
    );

    // ---------------------------------------------------------
    // BUYER UPDATE CART
    // ---------------------------------------------------------
    app().registerHandler(
        "/cart/{1}",
        [](const HttpRequestPtr &req,
           std::function<void(const HttpResponsePtr &)> &&callback,
           int cartId)
        {
            if (!hasRole(req, "BUYER"))
            {
                callback(jsonError(k403Forbidden, "Buyer access only"));
                return;
            }

            auto json = req->getJsonObject();

            if (!json)
            {
                callback(jsonError(k400BadRequest, "Invalid JSON"));
                return;
            }

            int buyerId = req->session()->get<int>("user_id");

            auto resp = HttpResponse::newHttpResponse();
            resp->setContentTypeCode(CT_APPLICATION_JSON);

            resp->setBody(
                Database::updateCart(
                    cartId,
                    buyerId,
                    (*json)["quantity"].asInt()
                )
            );

            callback(resp);
        },
        {Put}
    );

    // ---------------------------------------------------------
    // BUYER REMOVE CART ITEM
    // ---------------------------------------------------------
    app().registerHandler(
        "/cart/{1}",
        [](const HttpRequestPtr &req,
           std::function<void(const HttpResponsePtr &)> &&callback,
           int cartId)
        {
            if (!hasRole(req, "BUYER"))
            {
                callback(jsonError(k403Forbidden, "Buyer access only"));
                return;
            }

            int buyerId = req->session()->get<int>("user_id");

            auto resp = HttpResponse::newHttpResponse();
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            resp->setBody(Database::removeCartItem(cartId, buyerId));

            callback(resp);
        },
        {Delete}
    );

    // ---------------------------------------------------------
    // BUYER PLACE ORDER
    // ---------------------------------------------------------
    app().registerHandler(
        "/orders",
        [](const HttpRequestPtr &req,
           std::function<void(const HttpResponsePtr &)> &&callback)
        {
            if (!hasRole(req, "BUYER"))
            {
                callback(jsonError(k403Forbidden, "Buyer access only"));
                return;
            }

            int buyerId = req->session()->get<int>("user_id");

            auto resp = HttpResponse::newHttpResponse();
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            resp->setBody(Database::placeOrder(buyerId));

            callback(resp);
        },
        {Post}
    );

    // ---------------------------------------------------------
    // BUYER ORDERS
    // ---------------------------------------------------------
    app().registerHandler(
        "/orders",
        [](const HttpRequestPtr &req,
           std::function<void(const HttpResponsePtr &)> &&callback)
        {
            if (!hasRole(req, "BUYER"))
            {
                callback(jsonError(k403Forbidden, "Buyer access only"));
                return;
            }

            int buyerId = req->session()->get<int>("user_id");

            auto resp = HttpResponse::newHttpResponse();
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            resp->setBody(Database::getBuyerOrders(buyerId));

            callback(resp);
        },
        {Get}
    );

    // ---------------------------------------------------------
    // SELLER ORDERS
    // ---------------------------------------------------------
    app().registerHandler(
        "/seller/orders",
        [](const HttpRequestPtr &req,
           std::function<void(const HttpResponsePtr &)> &&callback)
        {
            if (!hasRole(req, "SELLER"))
            {
                callback(jsonError(k403Forbidden, "Seller access only"));
                return;
            }

            int sellerId = req->session()->get<int>("user_id");

            auto resp = HttpResponse::newHttpResponse();
            resp->setContentTypeCode(CT_APPLICATION_JSON);
            resp->setBody(Database::getSellerOrders(sellerId));

            callback(resp);
        },
        {Get}
    );

    // ---------------------------------------------------------
    // SELLER UPDATE ORDER STATUS
    // ---------------------------------------------------------
    app().registerHandler(
        "/orders/status/{1}",
        [](const HttpRequestPtr &req,
           std::function<void(const HttpResponsePtr &)> &&callback,
           int orderId)
        {
            if (!hasRole(req, "SELLER"))
            {
                callback(jsonError(k403Forbidden, "Seller access only"));
                return;
            }

            auto json = req->getJsonObject();

            if (!json)
            {
                callback(jsonError(k400BadRequest, "Invalid JSON"));
                return;
            }

            int sellerId = req->session()->get<int>("user_id");

            auto resp = HttpResponse::newHttpResponse();
            resp->setContentTypeCode(CT_APPLICATION_JSON);

            resp->setBody(
                Database::updateOrderStatus(
                    orderId,
                    sellerId,
                    (*json)["status"].asString()
                )
            );

            callback(resp);
        },
        {Put}
    );

    // ---------------------------------------------------------
    // SERVER
    // ---------------------------------------------------------
    if (!std::getenv("PRITHIVMART_DB_PASSWORD"))
    {
        std::cerr << "Warning: PRITHIVMART_DB_PASSWORD is not set. "
                  << "Database routes will fail until it is set."
                  << std::endl;
    }

    // FRONTEND
    // ---------------------------------------------------------
    // The frontend file is named: prithivmart.html
    // Keep prithivmart.html in the same folder from which
    // PrithivMart.exe is started.
    app().setDocumentRoot(".");
    app().setHomePage("prithivmart.html");

    app().addListener("127.0.0.1", 8080);

    std::cout
        << "PrithivMart running at http://127.0.0.1:8080"
        << std::endl;

    app().run();

    curl_global_cleanup();
    return 0;
}
