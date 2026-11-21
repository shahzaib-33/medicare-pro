# DRAP data sources (checked 21 November 2026)

| Page | What it offers | Download or API | Terms |
|---|---|---|---|
| https://www.dra.gov.pk/ | News, alerts, recalls, notices. Links to the e-services below. | Per notice PDF files only. No medicine dataset found. | None for data. |
| https://eapp.dra.gov.pk/WebProductIndex.php | "Registered Product Data", described as an expanded provisional list. Search by registration number, brand or generic name. | No export, no CSV or Excel, no documented API. | States the list "cannot and shall not be used as a reference for any purpose including but not limited to litigation, claim, right, research, citation, statistical analysis etc." |
| https://e.dra.gov.pk/ | e-Services portal with Registered Drugs Index, Drug Pricing Index, and Products, Batch and Barcode System. | No download link or API documented on the page. | None stated. |

## Decision

* No scraping and no undocumented API calls. Automated extraction is not offered and the
  registered products page disclaims reference use.
* The app imports official files through the administrator CSV import and through signed update
  packages built with `refpack`.
* Recommended step for SoftzenAgency: write to DRAP and ask for the registered products and price
  lists as files with permission to redistribute them inside commercial software. Record the
  reply and the access method in the source fields of the import.
* Prices are stored only when a file states the price source. Missing fields stay empty.

## CSV columns understood by the importer

Header names are matched without case. Unknown columns are reported and ignored.

`Brand Name` (required), `Urdu Brand Name`, `Generic Name`, `Urdu Generic Name`,
`Active Ingredients` (separate with + or ;), `Composition`, `Strength`, `Dosage Form`,
`Manufacturer`, `Marketing Authorization Holder`, `Registration No`, `Registration Date`,
`Status` (active, suspended, cancelled, discontinued), `Category`, `Pack Size`, `Barcode`
(8 to 14 digits), `Prescription`, `Price` (needs `Price Source`), `Verification Date`.
Save Excel files as "CSV UTF-8" before importing.
