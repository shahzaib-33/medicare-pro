#include "Schema.h"

namespace mcp::Schema {

const QStringList &referenceMigrations()
{
    static const QStringList m = {
        // v1: national reference catalog
        R"SQL(
CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT);

CREATE TABLE IF NOT EXISTS medicine_data_sources (
    id INTEGER PRIMARY KEY,
    code TEXT NOT NULL UNIQUE,
    name TEXT NOT NULL,
    url TEXT,
    access_method TEXT,
    terms_note TEXT,
    retrieved_at TEXT,
    notes TEXT
);

CREATE TABLE IF NOT EXISTS manufacturers_reference (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    name_norm TEXT NOT NULL UNIQUE,
    name_ur TEXT,
    licence_no TEXT,
    source_id INTEGER REFERENCES medicine_data_sources(id),
    updated_at TEXT
);

CREATE TABLE IF NOT EXISTS medicine_categories_reference (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    name_norm TEXT NOT NULL UNIQUE,
    name_ur TEXT
);

CREATE TABLE IF NOT EXISTS national_medicine_catalog (
    id INTEGER PRIMARY KEY,
    ref_key TEXT NOT NULL UNIQUE,
    brand_name TEXT NOT NULL,
    generic_name TEXT,
    composition TEXT,
    strength TEXT,
    dosage_form TEXT,
    manufacturer_id INTEGER REFERENCES manufacturers_reference(id),
    mah_name TEXT,
    registration_no TEXT,
    registration_date TEXT,
    registration_status TEXT NOT NULL DEFAULT 'unknown',
    category_id INTEGER REFERENCES medicine_categories_reference(id),
    pack_size TEXT,
    barcode TEXT,
    rx_class TEXT,
    reference_price REAL,
    price_source TEXT,
    source_id INTEGER REFERENCES medicine_data_sources(id),
    verification_status TEXT NOT NULL DEFAULT 'unverified',
    verified_at TEXT,
    last_updated TEXT NOT NULL,
    row_hash TEXT NOT NULL,
    version_added INTEGER NOT NULL DEFAULT 0,
    version_changed INTEGER NOT NULL DEFAULT 0,
    brand_norm TEXT NOT NULL,
    generic_norm TEXT
);
CREATE INDEX IF NOT EXISTS ix_cat_reg ON national_medicine_catalog(registration_no);
CREATE INDEX IF NOT EXISTS ix_cat_barcode ON national_medicine_catalog(barcode);
CREATE INDEX IF NOT EXISTS ix_cat_brand ON national_medicine_catalog(brand_norm);
CREATE INDEX IF NOT EXISTS ix_cat_generic ON national_medicine_catalog(generic_norm);
CREATE INDEX IF NOT EXISTS ix_cat_status ON national_medicine_catalog(registration_status);

CREATE TABLE IF NOT EXISTS medicine_translations (
    medicine_id INTEGER NOT NULL REFERENCES national_medicine_catalog(id) ON DELETE CASCADE,
    lang TEXT NOT NULL,
    field TEXT NOT NULL,
    value TEXT NOT NULL,
    value_norm TEXT NOT NULL,
    provenance TEXT NOT NULL DEFAULT 'manual',
    PRIMARY KEY (medicine_id, lang, field)
);

CREATE TABLE IF NOT EXISTS medicine_ingredients (
    medicine_id INTEGER NOT NULL REFERENCES national_medicine_catalog(id) ON DELETE CASCADE,
    position INTEGER NOT NULL,
    ingredient TEXT NOT NULL,
    ingredient_norm TEXT NOT NULL,
    strength TEXT,
    PRIMARY KEY (medicine_id, position)
);
CREATE INDEX IF NOT EXISTS ix_ingr_norm ON medicine_ingredients(ingredient_norm);

CREATE TABLE IF NOT EXISTS regulatory_registration_records (
    id INTEGER PRIMARY KEY,
    medicine_id INTEGER REFERENCES national_medicine_catalog(id) ON DELETE SET NULL,
    ref_key TEXT NOT NULL,
    registration_no TEXT,
    event TEXT NOT NULL,
    old_status TEXT,
    new_status TEXT,
    event_date TEXT,
    details TEXT,
    source_id INTEGER REFERENCES medicine_data_sources(id),
    db_version INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS ix_regrec_key ON regulatory_registration_records(ref_key);

CREATE TABLE IF NOT EXISTS medicine_database_versions (
    version INTEGER PRIMARY KEY,
    published_at TEXT,
    installed_at TEXT NOT NULL,
    description TEXT,
    record_count INTEGER NOT NULL,
    coverage_note TEXT,
    package_sha256 TEXT,
    kind TEXT NOT NULL
);

-- Token index for fast prefix search on any PC. kind: 1 word, 2 phonetic key, 3 code
CREATE TABLE IF NOT EXISTS search_index (
    term TEXT NOT NULL,
    kind INTEGER NOT NULL,
    medicine_id INTEGER NOT NULL REFERENCES national_medicine_catalog(id) ON DELETE CASCADE,
    weight INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS ix_search_term ON search_index(term, kind);
CREATE INDEX IF NOT EXISTS ix_search_med ON search_index(medicine_id);
)SQL",
    };
    return m;
}

const QStringList &pharmacyMigrations()
{
    // All money columns are INTEGER paisa. Quantities are INTEGER base units (tablet, ml, piece).
    static const QStringList m = {
        R"SQL(
CREATE TABLE IF NOT EXISTS app_settings (key TEXT PRIMARY KEY, value TEXT);
CREATE TABLE IF NOT EXISTS branches (id INTEGER PRIMARY KEY, name TEXT NOT NULL, address TEXT);
INSERT OR IGNORE INTO branches(id, name) VALUES(1, 'Main');

CREATE TABLE IF NOT EXISTS roles (
    id INTEGER PRIMARY KEY,
    code TEXT NOT NULL UNIQUE,
    name TEXT NOT NULL,
    system INTEGER NOT NULL DEFAULT 1
);
CREATE TABLE IF NOT EXISTS permissions (code TEXT PRIMARY KEY, description TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS role_permissions (
    role_id INTEGER NOT NULL REFERENCES roles(id) ON DELETE CASCADE,
    permission_code TEXT NOT NULL REFERENCES permissions(code),
    PRIMARY KEY (role_id, permission_code)
);
CREATE TABLE IF NOT EXISTS users (
    id INTEGER PRIMARY KEY,
    username TEXT NOT NULL UNIQUE COLLATE NOCASE,
    full_name TEXT NOT NULL,
    password_hash TEXT NOT NULL,
    password_salt TEXT NOT NULL,
    password_iterations INTEGER NOT NULL,
    role_id INTEGER NOT NULL REFERENCES roles(id),
    active INTEGER NOT NULL DEFAULT 1,
    must_change_password INTEGER NOT NULL DEFAULT 0,
    failed_attempts INTEGER NOT NULL DEFAULT 0,
    locked_until TEXT,
    created_at TEXT NOT NULL,
    last_login_at TEXT
);
-- Owner can grant or deny single permissions per user on top of the role.
CREATE TABLE IF NOT EXISTS user_permission_overrides (
    user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    permission_code TEXT NOT NULL REFERENCES permissions(code),
    allowed INTEGER NOT NULL,
    PRIMARY KEY (user_id, permission_code)
);
CREATE TABLE IF NOT EXISTS login_history (
    id INTEGER PRIMARY KEY,
    user_id INTEGER REFERENCES users(id),
    username TEXT,
    event TEXT NOT NULL,
    ts TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS audit_logs (
    id INTEGER PRIMARY KEY,
    ts TEXT NOT NULL,
    user_id INTEGER REFERENCES users(id),
    action TEXT NOT NULL,
    entity TEXT,
    entity_id INTEGER,
    details TEXT
);
CREATE INDEX IF NOT EXISTS ix_audit_ts ON audit_logs(ts);
CREATE TABLE IF NOT EXISTS employees (
    id INTEGER PRIMARY KEY,
    user_id INTEGER UNIQUE REFERENCES users(id),
    name TEXT NOT NULL,
    phone TEXT,
    designation TEXT,
    active INTEGER NOT NULL DEFAULT 1,
    joined_on TEXT,
    notes TEXT
);

CREATE TABLE IF NOT EXISTS manufacturers (id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE COLLATE NOCASE);
CREATE TABLE IF NOT EXISTS medicine_categories (id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE COLLATE NOCASE, name_ur TEXT);

CREATE TABLE IF NOT EXISTS suppliers (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    phone TEXT,
    address TEXT,
    notes TEXT,
    active INTEGER NOT NULL DEFAULT 1,
    created_at TEXT NOT NULL
);
-- credit increases what the pharmacy owes, debit reduces it.
CREATE TABLE IF NOT EXISTS supplier_ledger (
    id INTEGER PRIMARY KEY,
    supplier_id INTEGER NOT NULL REFERENCES suppliers(id),
    ts TEXT NOT NULL,
    entry_type TEXT NOT NULL,
    ref_type TEXT,
    ref_id INTEGER,
    debit INTEGER NOT NULL DEFAULT 0 CHECK (debit >= 0),
    credit INTEGER NOT NULL DEFAULT 0 CHECK (credit >= 0),
    note TEXT,
    user_id INTEGER REFERENCES users(id)
);
CREATE INDEX IF NOT EXISTS ix_supledger ON supplier_ledger(supplier_id, ts);

CREATE TABLE IF NOT EXISTS customers (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    phone TEXT,
    address TEXT,
    notes TEXT,
    credit_limit INTEGER NOT NULL DEFAULT 0,
    active INTEGER NOT NULL DEFAULT 1,
    created_at TEXT NOT NULL
);
-- debit increases what the customer owes, credit reduces it.
CREATE TABLE IF NOT EXISTS customer_ledger (
    id INTEGER PRIMARY KEY,
    customer_id INTEGER NOT NULL REFERENCES customers(id),
    ts TEXT NOT NULL,
    entry_type TEXT NOT NULL,
    ref_type TEXT,
    ref_id INTEGER,
    debit INTEGER NOT NULL DEFAULT 0 CHECK (debit >= 0),
    credit INTEGER NOT NULL DEFAULT 0 CHECK (credit >= 0),
    note TEXT,
    user_id INTEGER REFERENCES users(id)
);
CREATE INDEX IF NOT EXISTS ix_custledger ON customer_ledger(customer_id, ts);

CREATE TABLE IF NOT EXISTS medicines (
    id INTEGER PRIMARY KEY,
    reference_ref_key TEXT,
    reference_version INTEGER,
    reference_hash TEXT,
    brand_name TEXT NOT NULL,
    brand_name_ur TEXT,
    generic_name TEXT,
    generic_name_ur TEXT,
    active_ingredients TEXT,
    composition TEXT,
    strength TEXT,
    dosage_form TEXT,
    manufacturer TEXT,
    mah_name TEXT,
    registration_no TEXT,
    registration_status TEXT,
    category TEXT,
    pack_size TEXT,
    base_unit TEXT NOT NULL DEFAULT 'unit',
    units_per_pack INTEGER NOT NULL DEFAULT 1 CHECK (units_per_pack >= 1),
    barcode TEXT,
    purchase_price INTEGER,
    sale_price INTEGER,
    tax_rate_bp INTEGER NOT NULL DEFAULT 0 CHECK (tax_rate_bp >= 0),
    supplier_id INTEGER REFERENCES suppliers(id),
    rack_location TEXT,
    min_stock INTEGER NOT NULL DEFAULT 0,
    rx_required INTEGER NOT NULL DEFAULT 0,
    archived INTEGER NOT NULL DEFAULT 0,
    locally_edited INTEGER NOT NULL DEFAULT 0,
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL
);
CREATE UNIQUE INDEX IF NOT EXISTS ux_med_barcode ON medicines(barcode) WHERE barcode IS NOT NULL AND barcode <> '';
CREATE INDEX IF NOT EXISTS ix_med_refkey ON medicines(reference_ref_key);
CREATE INDEX IF NOT EXISTS ix_med_brand ON medicines(brand_name COLLATE NOCASE);
CREATE INDEX IF NOT EXISTS ix_med_generic ON medicines(generic_name COLLATE NOCASE);

-- Unit conversions, e.g. tablet 1, strip 10, box 100. factor is in base units.
CREATE TABLE IF NOT EXISTS medicine_units (
    medicine_id INTEGER NOT NULL REFERENCES medicines(id) ON DELETE CASCADE,
    unit_name TEXT NOT NULL,
    factor INTEGER NOT NULL CHECK (factor >= 1),
    PRIMARY KEY (medicine_id, unit_name)
);
CREATE TABLE IF NOT EXISTS medicine_barcodes (
    barcode TEXT PRIMARY KEY,
    medicine_id INTEGER NOT NULL REFERENCES medicines(id) ON DELETE CASCADE,
    unit_name TEXT
);

CREATE TABLE IF NOT EXISTS medicine_batches (
    id INTEGER PRIMARY KEY,
    medicine_id INTEGER NOT NULL REFERENCES medicines(id),
    batch_no TEXT NOT NULL,
    mfg_date TEXT,
    expiry_date TEXT NOT NULL,
    supplier_id INTEGER REFERENCES suppliers(id),
    unit_cost INTEGER NOT NULL DEFAULT 0,
    sale_price INTEGER,
    qty_received INTEGER NOT NULL DEFAULT 0,
    qty_available INTEGER NOT NULL DEFAULT 0 CHECK (qty_available >= 0),
    qty_quarantine INTEGER NOT NULL DEFAULT 0 CHECK (qty_quarantine >= 0),
    qty_damaged INTEGER NOT NULL DEFAULT 0 CHECK (qty_damaged >= 0),
    status TEXT NOT NULL DEFAULT 'active',
    status_note TEXT,
    created_at TEXT NOT NULL,
    UNIQUE (medicine_id, batch_no)
);
CREATE INDEX IF NOT EXISTS ix_batch_med_exp ON medicine_batches(medicine_id, expiry_date);
CREATE INDEX IF NOT EXISTS ix_batch_exp ON medicine_batches(expiry_date);

-- Every stock change. qty is signed, in base units. bucket: available, quarantine, damaged.
CREATE TABLE IF NOT EXISTS stock_movements (
    id INTEGER PRIMARY KEY,
    ts TEXT NOT NULL,
    medicine_id INTEGER NOT NULL REFERENCES medicines(id),
    batch_id INTEGER NOT NULL REFERENCES medicine_batches(id),
    bucket TEXT NOT NULL,
    qty INTEGER NOT NULL,
    reason TEXT NOT NULL,
    ref_type TEXT,
    ref_id INTEGER,
    unit_cost INTEGER,
    user_id INTEGER REFERENCES users(id),
    note TEXT
);
CREATE INDEX IF NOT EXISTS ix_move_med ON stock_movements(medicine_id, ts);
CREATE INDEX IF NOT EXISTS ix_move_batch ON stock_movements(batch_id);

CREATE TABLE IF NOT EXISTS stock_adjustments (
    id INTEGER PRIMARY KEY,
    ts TEXT NOT NULL,
    user_id INTEGER REFERENCES users(id),
    batch_id INTEGER NOT NULL REFERENCES medicine_batches(id),
    kind TEXT NOT NULL,
    qty INTEGER NOT NULL,
    reason TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS stock_counts (
    id INTEGER PRIMARY KEY,
    started_at TEXT NOT NULL,
    finished_at TEXT,
    user_id INTEGER REFERENCES users(id),
    status TEXT NOT NULL,
    note TEXT
);
CREATE TABLE IF NOT EXISTS stock_count_items (
    count_id INTEGER NOT NULL REFERENCES stock_counts(id) ON DELETE CASCADE,
    batch_id INTEGER NOT NULL REFERENCES medicine_batches(id),
    expected INTEGER NOT NULL,
    counted INTEGER,
    PRIMARY KEY (count_id, batch_id)
);

CREATE TABLE IF NOT EXISTS purchase_invoices (
    id INTEGER PRIMARY KEY,
    supplier_id INTEGER NOT NULL REFERENCES suppliers(id),
    invoice_no TEXT NOT NULL,
    invoice_date TEXT NOT NULL,
    subtotal INTEGER NOT NULL,
    discount INTEGER NOT NULL DEFAULT 0,
    tax INTEGER NOT NULL DEFAULT 0,
    total INTEGER NOT NULL,
    paid INTEGER NOT NULL DEFAULT 0,
    status TEXT NOT NULL DEFAULT 'posted',
    notes TEXT,
    user_id INTEGER REFERENCES users(id),
    created_at TEXT NOT NULL,
    UNIQUE (supplier_id, invoice_no)
);
CREATE TABLE IF NOT EXISTS purchase_items (
    id INTEGER PRIMARY KEY,
    invoice_id INTEGER NOT NULL REFERENCES purchase_invoices(id),
    medicine_id INTEGER NOT NULL REFERENCES medicines(id),
    batch_id INTEGER NOT NULL REFERENCES medicine_batches(id),
    qty INTEGER NOT NULL CHECK (qty > 0),
    bonus_qty INTEGER NOT NULL DEFAULT 0,
    unit_cost INTEGER NOT NULL,
    discount INTEGER NOT NULL DEFAULT 0,
    tax INTEGER NOT NULL DEFAULT 0,
    line_total INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS purchase_payments (
    id INTEGER PRIMARY KEY,
    supplier_id INTEGER NOT NULL REFERENCES suppliers(id),
    invoice_id INTEGER REFERENCES purchase_invoices(id),
    ts TEXT NOT NULL,
    amount INTEGER NOT NULL CHECK (amount > 0),
    method TEXT NOT NULL,
    reference TEXT,
    user_id INTEGER REFERENCES users(id)
);
CREATE TABLE IF NOT EXISTS purchase_returns (
    id INTEGER PRIMARY KEY,
    supplier_id INTEGER NOT NULL REFERENCES suppliers(id),
    invoice_id INTEGER REFERENCES purchase_invoices(id),
    ts TEXT NOT NULL,
    total INTEGER NOT NULL,
    reason TEXT NOT NULL,
    user_id INTEGER REFERENCES users(id)
);
CREATE TABLE IF NOT EXISTS purchase_return_items (
    id INTEGER PRIMARY KEY,
    return_id INTEGER NOT NULL REFERENCES purchase_returns(id),
    batch_id INTEGER NOT NULL REFERENCES medicine_batches(id),
    bucket TEXT NOT NULL,
    qty INTEGER NOT NULL CHECK (qty > 0),
    unit_cost INTEGER NOT NULL,
    line_total INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS cash_shifts (
    id INTEGER PRIMARY KEY,
    user_id INTEGER NOT NULL REFERENCES users(id),
    opened_at TEXT NOT NULL,
    closed_at TEXT,
    opening_cash INTEGER NOT NULL,
    expected_cash INTEGER,
    actual_cash INTEGER,
    variance INTEGER,
    status TEXT NOT NULL DEFAULT 'open',
    notes TEXT
);
CREATE TABLE IF NOT EXISTS cash_movements (
    id INTEGER PRIMARY KEY,
    shift_id INTEGER NOT NULL REFERENCES cash_shifts(id),
    ts TEXT NOT NULL,
    kind TEXT NOT NULL,
    amount INTEGER NOT NULL,
    ref_type TEXT,
    ref_id INTEGER,
    note TEXT
);

CREATE TABLE IF NOT EXISTS prescriptions (
    id INTEGER PRIMARY KEY,
    ref_no TEXT NOT NULL,
    customer_id INTEGER REFERENCES customers(id),
    patient_name TEXT,
    doctor_name TEXT,
    issued_date TEXT,
    notes TEXT,
    status TEXT NOT NULL DEFAULT 'pending',
    reviewed_by INTEGER REFERENCES users(id),
    reviewed_at TEXT,
    review_note TEXT,
    created_by INTEGER REFERENCES users(id),
    created_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS prescription_items (
    id INTEGER PRIMARY KEY,
    prescription_id INTEGER NOT NULL REFERENCES prescriptions(id) ON DELETE CASCADE,
    medicine_id INTEGER REFERENCES medicines(id),
    medicine_text TEXT,
    qty INTEGER,
    directions TEXT
);

CREATE TABLE IF NOT EXISTS sales (
    id INTEGER PRIMARY KEY,
    invoice_no TEXT NOT NULL UNIQUE,
    ts TEXT NOT NULL,
    customer_id INTEGER REFERENCES customers(id),
    user_id INTEGER REFERENCES users(id),
    shift_id INTEGER REFERENCES cash_shifts(id),
    prescription_id INTEGER REFERENCES prescriptions(id),
    subtotal INTEGER NOT NULL,
    discount INTEGER NOT NULL DEFAULT 0,
    tax INTEGER NOT NULL DEFAULT 0,
    total INTEGER NOT NULL,
    paid INTEGER NOT NULL DEFAULT 0,
    change_given INTEGER NOT NULL DEFAULT 0,
    cost_total INTEGER NOT NULL DEFAULT 0,
    status TEXT NOT NULL DEFAULT 'completed',
    offline INTEGER NOT NULL DEFAULT 0,
    notes TEXT
);
CREATE INDEX IF NOT EXISTS ix_sales_ts ON sales(ts);
CREATE TABLE IF NOT EXISTS sale_items (
    id INTEGER PRIMARY KEY,
    sale_id INTEGER NOT NULL REFERENCES sales(id),
    medicine_id INTEGER NOT NULL REFERENCES medicines(id),
    qty INTEGER NOT NULL CHECK (qty > 0),
    unit_price INTEGER NOT NULL,
    discount INTEGER NOT NULL DEFAULT 0,
    tax INTEGER NOT NULL DEFAULT 0,
    line_total INTEGER NOT NULL,
    cost_total INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS ix_saleitems_sale ON sale_items(sale_id);
CREATE TABLE IF NOT EXISTS sale_batches (
    id INTEGER PRIMARY KEY,
    sale_item_id INTEGER NOT NULL REFERENCES sale_items(id),
    batch_id INTEGER NOT NULL REFERENCES medicine_batches(id),
    qty INTEGER NOT NULL CHECK (qty > 0),
    unit_cost INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS sale_payments (
    id INTEGER PRIMARY KEY,
    sale_id INTEGER NOT NULL REFERENCES sales(id),
    method TEXT NOT NULL,
    amount INTEGER NOT NULL CHECK (amount > 0),
    reference TEXT
);
CREATE TABLE IF NOT EXISTS held_bills (
    id INTEGER PRIMARY KEY,
    ts TEXT NOT NULL,
    user_id INTEGER REFERENCES users(id),
    label TEXT,
    payload TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS receipt_prints (
    id INTEGER PRIMARY KEY,
    sale_id INTEGER NOT NULL REFERENCES sales(id),
    ts TEXT NOT NULL,
    user_id INTEGER REFERENCES users(id),
    copy INTEGER NOT NULL,
    format TEXT
);
CREATE TABLE IF NOT EXISTS sales_returns (
    id INTEGER PRIMARY KEY,
    sale_id INTEGER NOT NULL REFERENCES sales(id),
    ts TEXT NOT NULL,
    user_id INTEGER REFERENCES users(id),
    total INTEGER NOT NULL,
    cost_total INTEGER NOT NULL DEFAULT 0,
    refund_method TEXT NOT NULL,
    reason TEXT NOT NULL,
    shift_id INTEGER REFERENCES cash_shifts(id)
);
CREATE TABLE IF NOT EXISTS sales_return_items (
    id INTEGER PRIMARY KEY,
    return_id INTEGER NOT NULL REFERENCES sales_returns(id),
    sale_item_id INTEGER NOT NULL REFERENCES sale_items(id),
    batch_id INTEGER NOT NULL REFERENCES medicine_batches(id),
    qty INTEGER NOT NULL CHECK (qty > 0),
    amount INTEGER NOT NULL,
    unit_cost INTEGER NOT NULL,
    disposition TEXT NOT NULL DEFAULT 'pending',
    inspected_by INTEGER REFERENCES users(id),
    inspected_at TEXT
);

CREATE TABLE IF NOT EXISTS expense_categories (id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE COLLATE NOCASE);
CREATE TABLE IF NOT EXISTS expenses (
    id INTEGER PRIMARY KEY,
    category_id INTEGER NOT NULL REFERENCES expense_categories(id),
    expense_date TEXT NOT NULL,
    description TEXT NOT NULL,
    amount INTEGER NOT NULL CHECK (amount > 0),
    method TEXT NOT NULL,
    reference TEXT,
    notes TEXT,
    user_id INTEGER REFERENCES users(id),
    shift_id INTEGER REFERENCES cash_shifts(id),
    status TEXT NOT NULL DEFAULT 'posted',
    reversal_of INTEGER REFERENCES expenses(id),
    created_at TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS ix_exp_date ON expenses(expense_date);

CREATE TABLE IF NOT EXISTS backups (
    id INTEGER PRIMARY KEY,
    ts TEXT NOT NULL,
    path TEXT NOT NULL,
    size INTEGER,
    sha256 TEXT,
    kind TEXT NOT NULL,
    ok INTEGER NOT NULL,
    message TEXT
);

-- Local products whose official registration changed. Admin decides what to do.
CREATE TABLE IF NOT EXISTS medicine_review_flags (
    id INTEGER PRIMARY KEY,
    medicine_id INTEGER NOT NULL REFERENCES medicines(id),
    ref_key TEXT NOT NULL,
    reason TEXT NOT NULL,
    old_value TEXT,
    new_value TEXT,
    reference_version INTEGER,
    created_at TEXT NOT NULL,
    resolved_at TEXT,
    resolution TEXT
);
CREATE TABLE IF NOT EXISTS medicine_update_history (
    id INTEGER PRIMARY KEY,
    kind TEXT NOT NULL,
    from_version INTEGER,
    to_version INTEGER,
    started_at TEXT NOT NULL,
    finished_at TEXT,
    status TEXT NOT NULL,
    added INTEGER DEFAULT 0,
    updated INTEGER DEFAULT 0,
    deactivated INTEGER DEFAULT 0,
    skipped INTEGER DEFAULT 0,
    flagged INTEGER DEFAULT 0,
    backup_path TEXT,
    message TEXT
);
CREATE TABLE IF NOT EXISTS medicine_update_logs (
    id INTEGER PRIMARY KEY,
    history_id INTEGER REFERENCES medicine_update_history(id),
    ts TEXT NOT NULL,
    level TEXT NOT NULL,
    message TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS software_versions (
    version TEXT PRIMARY KEY,
    first_run_at TEXT NOT NULL,
    notes TEXT
);
CREATE TABLE IF NOT EXISTS software_update_history (
    id INTEGER PRIMARY KEY,
    from_version TEXT,
    to_version TEXT,
    started_at TEXT NOT NULL,
    finished_at TEXT,
    status TEXT NOT NULL,
    backup_path TEXT,
    message TEXT
);
CREATE TABLE IF NOT EXISTS update_manifests (
    id INTEGER PRIMARY KEY,
    fetched_at TEXT NOT NULL,
    url TEXT NOT NULL,
    sequence INTEGER,
    sha256 TEXT NOT NULL,
    signature_ok INTEGER NOT NULL,
    body TEXT
);
CREATE TABLE IF NOT EXISTS update_download_history (
    id INTEGER PRIMARY KEY,
    item_type TEXT NOT NULL,
    version TEXT,
    url TEXT NOT NULL,
    bytes INTEGER,
    sha256 TEXT,
    ok INTEGER NOT NULL,
    started_at TEXT NOT NULL,
    finished_at TEXT,
    message TEXT
);
CREATE TABLE IF NOT EXISTS update_security_logs (
    id INTEGER PRIMARY KEY,
    ts TEXT NOT NULL,
    event TEXT NOT NULL,
    detail TEXT
);
)SQL",
    };
    return m;
}
}
