# MediCare Pro 💊

Pharmacy management and point of sale software for Pakistan by SoftzenAgency.
Windows 10 and 11, 64-bit. C++20, Qt 6 Widgets, SQLite, CMake, Qt Charts, Qt PrintSupport, Inno Setup.
Works fully offline. English and Urdu.

## Modules

| Area | What it does |
| --- | --- |
| Sign in and security | First run owner setup, 6 roles (Super Admin, Pharmacy Owner, Pharmacist, Cashier, Inventory Manager, Accountant), 42 permissions, per user allow or deny, PBKDF2-SHA256 passwords, lock after 5 wrong attempts, auto lock when idle, login history, audit log of every important action. |
| Dashboard | Live figures from the database with Today, Yesterday, This week, This month, Last month, This year and custom dates. Sales, profit, expenses, purchases, receivables, payables, low stock, expiry alerts, waiting prescriptions, returns to inspect. Charts for daily sales, payment methods and top medicines. |
| Point of Sale | Barcode or name search (English, generic, Urdu), first expiry first out batches, line and bill discounts, sales tax, split payments (cash, bank transfer, card, JazzCash, Easypaisa, Raast, customer credit), change, hold and resume, cancel, receipt. Expired, recalled or quarantined stock is never sold. No negative stock. Each sale is one database transaction. |
| Receipts | 58 mm, 80 mm and A4, English or Urdu, print preview, PDF. Pharmacy name, address, phone, licence and NTN from Settings. Reprints are marked DUPLICATE COPY with a number and logged. |
| Sales history and returns | Search by date, invoice or customer. Return items with cash, wallet or customer credit refund. Returned stock goes to quarantine until a pharmacist inspects it. |
| Medicines | Add manually or from the Pakistan Medicine Database, edit, archive, restore, safe delete, units (strip, box) and extra barcodes, opening stock, CSV import and export. |
| Inventory and expiry | Batches with mfg and expiry dates, alerts for expired and 7, 30, 60, 90 days, damage, corrections, disposal, recall, stock count with live reconciliation, full movement log with reason and user. |
| Purchases | Supplier invoices with batch, mfg and expiry dates, bonus units, discount, tax, sale price per batch, full or partial payment, duplicate invoice check, returns to supplier, reversal instead of editing. |
| Suppliers and customers | Ledgers, opening balances, payments, credit limits, printable statements. |
| Prescriptions | Record prescriptions and items. Pharmacist approves or rejects. Prescription medicines need an approved prescription or a pharmacist at the counter. |
| Expenses and cash shifts | Expense categories, reversal instead of delete. Open shift, cash in, cash out, close with counted cash and difference. |
| Reports | Profit and loss, sales by day, invoices, profit by medicine, payments, cashiers, returns, tax, purchases, expenses, supplier and customer balances, stock valuation, low stock, expiry, movements, shifts, audit log, login history. Print, PDF and CSV. |
| Pakistan Medicine Database | Reference catalog kept apart from pharmacy data, bilingual search, official CSV import, signed online updates with backup and rollback. A reference update never changes your stock or sales. A new official price in an update becomes the sale price, which can be turned off. |
| Backup and restore | Automatic daily backup, manual backup, backup to USB, SHA-256 and integrity check, restore with a safety copy first. |
| Settings | Pharmacy profile, receipt paper and language, auto lock, English or Urdu, Windows 2003, Classic (Windows 98), light or dark theme, Light mode for low end PCs, offline switch. |

All money is stored as whole paisa in 64 bit integers. No floating point is used in totals.
The code is layered: screens (`src/ui`) call services (`src/services`), services use the database (`src/db`).
Permission checks run inside the services, not only in the screens.

## Install and first use

1. Run `MediCare_Pro_Setup.exe` and follow the steps.
2. Start MediCare Pro from the desktop icon 💊.
3. The first start asks for the pharmacy name and creates the owner account.
4. Open Settings and fill in the pharmacy address, phone, licence number and receipt paper size.
5. Add suppliers, then medicines (from the Pakistan Medicine Database or manually).
6. Enter stock through Purchases, or Opening stock for stock you already have.
7. Add staff in Users and permissions. Each user picks their own password at first sign in.
8. Open a cash shift and start selling on Point of Sale.

POS keys: F2 search, F4 barcode, F6 hold, F7 held bills, F8 cash received, F9 save sale, F12 print invoice, Delete removes the selected line.

## Price changes

In Pakistan a new price applies to all stock on the shelf, old and new.
MediCare Pro works the same way. The price can go up or down.
1. Purchase: put the new Sale price on the purchase line. Every batch of that medicine then sells at the new price.
2. Online: the app checks for medicine data updates once a day. A new official price in the update becomes the sale price by itself. Turn this off in Medicine Database Updates.
Every price change is saved in the audit log with the old and new price.

## Looks

The default look is Windows 2003.
Beige windows, soft rounded buttons, blue title bars and colour icons.
A blue task pane on the left groups every module, like the Windows Explorer side bar. Click a group title to fold it.
A menu bar with File, Edit, View, Inventory, Sales, Reports, Tools and Help.
A toolbar with colour icons and a status bar with user, date, time and database status.
Settings > Theme also offers Classic (Windows 98), Light and Dark.

## Low end and high end PCs

* Minimum: Windows 10 64-bit, 2 GB RAM, any dual core CPU, 1366x768 screen. No graphics card needed.
* At start the app reads RAM and CPU threads. On 4 GB RAM or less, or 2 threads or less, it uses
  Light mode: smaller SQLite cache, no memory mapping, no menu animations, slower search typing
  delay and shorter result lists. Settings > Performance lets the owner force Light or Standard.
* Search uses an indexed token table, so it stays fast on large catalogs (5 searches on 20,000
  records took under 0.1 second in the tests).
* The window opens before any network work. The first update check waits 90 seconds and never
  blocks the screen. A damaged reference catalog never stops the pharmacy: the app starts with an
  empty one and keeps the damaged file for recovery.
* High DPI screens are scaled by Qt automatically. Only one copy runs at a time per data folder.

## DRAP data

See `docs/DRAP_DATA_SOURCES.md`. In short: DRAP's public pages offer search only. They have no
download, export or public API, and the registered products page says the list cannot be used as
a reference for any purpose. The app therefore does not scrape DRAP. It imports official files
that DRAP or another authorised source supplies, and it always shows coverage, source and
verification status. The files in `data/sample` are demonstration data only.

## Build on Windows

1. Install Visual Studio 2022 (Desktop development with C++), Qt 6.5 or newer for MSVC 64-bit
   with the Qt Charts module and the OpenSSL 3 tool, and Inno Setup 6.
2. Edit the paths at the top of `build_windows.bat`.
3. Run `build_windows.bat`. It builds, runs all tests, prepares `deploy\` and creates
   `dist\MediCare_Pro_Setup.exe`.

Or push the project to GitHub. The workflow in `.github/workflows/windows-build.yml` builds,
tests and uploads the installer as an artifact.

## Turn on online updates

1. Run `refpack keygen <folder>` once. Keep `update_private.pem` offline and safe.
2. Copy `update_public.pem` over `resources/keys/update_public.pem`.
3. Put your update server address in `resources/config/update.json`, for example
   `"manifest_url": "https://updates.example.com/medicarepro/stable/manifest.json"` and
   `"allowed_hosts": ["updates.example.com"]`.
4. Rebuild. See `docs/UPDATE_ARCHITECTURE.md` for publishing packages.

Until this is done the app shows "update server is not configured" and works normally, with CSV
import available to administrators.

## Tests

`ctest` runs 5 suites (35 test cases): text normalization, import and search, update security and
rollback, the medicine database acceptance flow, and the business workflows from section 27 of the
master spec (purchase 100, sell 10, return 2, expired sale blocked, FEFO, financial calculations,
offline mode, reference update keeps local data, backup and restore, permissions, receipts, reports).
See `docs/ACCEPTANCE_TESTS.md`.

## Data folder

`%LOCALAPPDATA%\SoftzenAgency\MediCarePro\` holds `pharmacy.db`, `reference.db`, `backups\`,
`downloads\` and `settings.ini`. A `data` folder next to the exe switches to portable mode.

## Project layout

```
src/app        paths, performance profile, version
src/core       Money (integer paisa)
src/db         SQLite connections, migrations, schema
src/services   business rules: auth, inventory, purchases, sales, backup, reports, receipts, settings
src/refdata    Pakistan medicine reference catalog, search, CSV import
src/update     signed update packages, update client, software updater
src/ui         all screens
tests          automated tests
tools/refpack  SoftzenAgency tool to build and sign update packages
installer      Inno Setup script
resources      icon, update config, public key
```

## Licences of bundled files

Fonts (Inter, Noto Naskh Arabic, Noto Nastaliq Urdu), built into the app: SIL Open Font License 1.1 (`resources/fonts/OFL.txt`).
Interface icons: Lucide, ISC License (`resources/icons/ui/LICENSE_lucide.txt`).
Windows 2003 colour icons (`resources/icons/xp`): drawn for MediCare Pro, same licence as the app.
App icon: pill emoji from Noto Emoji, Apache License 2.0 (`resources/icons/NOTICE_emoji.txt`).
