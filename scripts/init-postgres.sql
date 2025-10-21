-- BrinkByte Vision - tAPI Edge Database Initialization
-- This script runs once when PostgreSQL container first starts
-- It sets up extensions and creates basic monitoring infrastructure

-- Enable UUID generation extension
CREATE EXTENSION IF NOT EXISTS "uuid-ossp";

-- Enable pg_stat_statements for query performance monitoring
CREATE EXTENSION IF NOT EXISTS pg_stat_statements;

-- Enable pg_trgm for text search optimization (useful for search features)
CREATE EXTENSION IF NOT EXISTS pg_trgm;

-- Create a function to update updated_at timestamps automatically
CREATE OR REPLACE FUNCTION update_updated_at_column()
RETURNS TRIGGER AS $$
BEGIN
    NEW.updated_at = CURRENT_TIMESTAMP;
    RETURN NEW;
END;
$$ language 'plpgsql';

-- Create a function to automatically create monthly partitions for usage_events
CREATE OR REPLACE FUNCTION create_monthly_partition(table_name TEXT, start_date DATE)
RETURNS VOID AS $$
DECLARE
    partition_name TEXT;
    start_ts TIMESTAMP;
    end_ts TIMESTAMP;
BEGIN
    partition_name := table_name || '_y' || TO_CHAR(start_date, 'YYYY') || 'm' || TO_CHAR(start_date, 'MM');
    start_ts := start_date;
    end_ts := start_date + INTERVAL '1 month';
    
    -- Check if partition already exists
    IF NOT EXISTS (
        SELECT 1 FROM pg_class WHERE relname = partition_name
    ) THEN
        EXECUTE format(
            'CREATE TABLE %I PARTITION OF %I FOR VALUES FROM (%L) TO (%L)',
            partition_name, table_name, start_ts, end_ts
        );
        
        RAISE NOTICE 'Created partition % for range % to %', partition_name, start_ts, end_ts;
    END IF;
END;
$$ LANGUAGE plpgsql;

-- Create a function to auto-create next month's partition
CREATE OR REPLACE FUNCTION ensure_future_partitions()
RETURNS VOID AS $$
DECLARE
    current_month DATE;
    i INTEGER;
BEGIN
    current_month := DATE_TRUNC('month', CURRENT_DATE);
    
    -- Create partitions for current month and next 3 months
    FOR i IN 0..3 LOOP
        PERFORM create_monthly_partition('usage_events', current_month + (i || ' months')::INTERVAL);
        PERFORM create_monthly_partition('telemetry_events', current_month + (i || ' months')::INTERVAL);
    END LOOP;
END;
$$ LANGUAGE plpgsql;

-- Create monitoring view for database health
CREATE OR REPLACE VIEW database_health AS
SELECT
    'connections' as metric,
    COUNT(*)::TEXT as value,
    CURRENT_TIMESTAMP as checked_at
FROM pg_stat_activity
UNION ALL
SELECT
    'database_size_mb' as metric,
    (pg_database_size(current_database()) / 1024 / 1024)::TEXT as value,
    CURRENT_TIMESTAMP as checked_at
UNION ALL
SELECT
    'cache_hit_ratio' as metric,
    ROUND(
        100.0 * sum(blks_hit) / NULLIF(sum(blks_hit) + sum(blks_read), 0),
        2
    )::TEXT as value,
    CURRENT_TIMESTAMP as checked_at
FROM pg_stat_database
WHERE datname = current_database();

-- Log successful initialization
DO $$
BEGIN
    RAISE NOTICE 'BrinkByte Vision tAPI PostgreSQL database initialized successfully';
    RAISE NOTICE 'Database: %', current_database();
    RAISE NOTICE 'Version: %', version();
END $$;

