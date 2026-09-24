# PrithivMart Modules

PrithivMart is organized around the mandatory capstone modules from the project specification.

## Mandatory Modules

1. User Registration & Login
   - Buyer and Seller registration
   - OTP/email verification
   - Login/logout
   - Role-based access

2. Seller Product Management
   - Add product
   - View own products
   - Edit product
   - Delete product
   - Manage price and stock

3. Buyer Shopping
   - Browse products
   - Search products
   - Filter by category
   - View product information

4. Shopping Cart
   - Add product
   - Update quantity
   - Remove product
   - Calculate cart total

5. Checkout
   - Confirm cart
   - Create order
   - Mock/no-real-payment flow

6. Order History & Order Status
   - Buyer order history
   - Seller received orders
   - Order status updates

7. Admin Panel
   - View users
   - View orders
   - Monitor products
   - Remove inappropriate listings

8. Product Reviews
   - Buyer rating
   - Buyer comments
   - Product review display

## Supporting Components

- Database: PostgreSQL
- Backend: C++ + Drogon
- Frontend: HTML + CSS + JavaScript
- Build: CMake
- Dependency management: vcpkg
- Source control: GitHub

## Current Repository Status

The current main.cc already contains backend logic for authentication, products, cart, checkout/order creation, buyer orders, and seller orders. Admin and product-review functionality still need to be implemented as application features.
