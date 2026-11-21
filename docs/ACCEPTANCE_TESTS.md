# Acceptance tests (spec section N)

Automated: `tests/test_acceptance.cpp` runs steps 2 to 17 headless on every build.
Result on the build machine (Linux, Qt 6.4.2, GCC 13): all 5 suites pass, 35 test cases.

| # | Step | Automated check | Manual check on Windows |
|---|---|---|---|
| 1 | Install on a fresh Windows PC | Not possible headless | Run the setup exe on a clean Windows 10 and 11 PC |
| 2 | Preloaded catalog available | Catalog count above 0, version and date set | Dashboard card shows count, version, date |
| 3 | Search Panadol | First hit is Panadol | Type Panadol in Pakistan Medicine Database |
| 4 | Search پیناڈول | First hit is Panadol | Switch to Urdu and search |
| 5 | Search Paracetamol | All paracetamol products found | Same |
| 6 | Add from catalog | Medicine created, no stock, no batch | Use the Add button |
| 7 | Set price and barcode | Saved on the pharmacy record | Same |
| 8 | Disconnect internet | Offline mode forced | Unplug network, check banner |
| 9 | Search offline | Works | Same |
| 10 | Offline sale | Sale saved, stock reduced | POS module |
| 11 | Reconnect | Online mode | Plug in network |
| 12 | Check for new records | Manifest checks covered in test_updates | Needs the update server set up |
| 13 | Download update | Package verification covered in test_updates | Same |
| 14 | New medicines available | New record found by search | Same |
| 15 | Prices and stock unchanged | Checked field by field | Open the medicine |
| 16 | Update history | History row with status installed | Medicine Database Updates page |
| 17 | Backup and rollback | Failed install rolls back, backup restore works | Restore Previous Version button |

# Business workflow tests (master spec section 27)

Automated: `tests/test_workflows.cpp` runs headless on every build.

| Test | What it proves |
|---|---|
| purchaseSellReturn | Purchase creates batches, sale reduces stock, return puts stock back and refunds the right amount |
| expiredSaleBlocked | Expired batches cannot be sold |
| fefo | Sales take the batch that expires first |
| financialCalculations | Discount, tax and totals in paisa with no rounding drift |
| holdShiftExpense | Held bills, shift open and close, cash expected versus counted, expenses |
| purchaseReturnAndReverse | Purchase return and purchase reversal keep stock and supplier balance correct |
| permissionsAndLogin | Roles block actions, wrong passwords lock the account, audit log rows |
| offlineMode | Every local operation works with the network forced off |
| referenceUpdateKeepsLocalData | Reference update never changes local stock or barcode. Prices change only with an official price and price apply on |
| backupRestoreIntegrity | Backup checksum, verify, restore and safety backup |
| receiptsAndReports | Receipt HTML in English and Urdu, duplicate copy mark, all reports build and export CSV |

Manual check on Windows: install MediCare_Pro_Setup.exe, finish the owner setup wizard, then repeat the same flows on screen.
