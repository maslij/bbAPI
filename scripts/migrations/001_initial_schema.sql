-- BrinkByte Vision - tAPI Core Schema Migration
-- Migration 001: Initial PostgreSQL Schema
-- This migrates all existing SQLite tables to PostgreSQL with enhancements

-- ======================================================
-- Core System Configuration
-- ======================================================

CREATE TABLE IF NOT EXISTS config (
    id SERIAL PRIMARY KEY,
    key VARCHAR(255) UNIQUE NOT NULL,
    value TEXT NOT NULL,
    value_type VARCHAR(50) DEFAULT 'string',  -- string, int, float, bool, json
    category VARCHAR(100) DEFAULT 'general',
    description TEXT,
    created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX idx_config_key ON config(key);
CREATE INDEX idx_config_category ON config(category);

-- Trigger to auto-update updated_at
CREATE TRIGGER update_config_updated_at
    BEFORE UPDATE ON config
    FOR EACH ROW
    EXECUTE FUNCTION update_updated_at_column();

-- ======================================================
-- Camera Management
-- ======================================================

CREATE TABLE IF NOT EXISTS cameras (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    camera_id VARCHAR(255) UNIQUE NOT NULL,
    tenant_id UUID,  -- Multi-tenancy support
    name VARCHAR(255) NOT NULL,
    url TEXT NOT NULL,
    status VARCHAR(50) DEFAULT 'stopped',  -- stopped, starting, running, error, paused
    pipeline_config JSONB DEFAULT '{}',  -- Full pipeline configuration as JSON
    metadata JSONB DEFAULT '{}',  -- Additional metadata (location, tags, etc.)
    is_active BOOLEAN DEFAULT true,
    created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX idx_cameras_camera_id ON cameras(camera_id);
CREATE INDEX idx_cameras_tenant_id ON cameras(tenant_id);
CREATE INDEX idx_cameras_status ON cameras(status);
CREATE INDEX idx_cameras_active ON cameras(is_active);

CREATE TRIGGER update_cameras_updated_at
    BEFORE UPDATE ON cameras
    FOR EACH ROW
    EXECUTE FUNCTION update_updated_at_column();

-- ======================================================
-- Camera Components (Source, Processor, Sink)
-- ======================================================

CREATE TABLE IF NOT EXISTS camera_components (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    camera_id VARCHAR(255) NOT NULL REFERENCES cameras(camera_id) ON DELETE CASCADE,
    component_type VARCHAR(50) NOT NULL,  -- source, processor, sink
    component_subtype VARCHAR(100) NOT NULL,  -- rtsp, object_detection, database, etc.
    component_config JSONB DEFAULT '{}',
    execution_order INTEGER DEFAULT 0,
    is_enabled BOOLEAN DEFAULT true,
    created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX idx_components_camera ON camera_components(camera_id);
CREATE INDEX idx_components_type ON camera_components(component_type);
CREATE INDEX idx_components_enabled ON camera_components(is_enabled);

CREATE TRIGGER update_camera_components_updated_at
    BEFORE UPDATE ON camera_components
    FOR EACH ROW
    EXECUTE FUNCTION update_updated_at_column();

-- ======================================================
-- Line Zones (for line crossing detection)
-- ======================================================

CREATE TABLE IF NOT EXISTS line_zones (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    zone_id VARCHAR(255) UNIQUE NOT NULL,
    camera_id VARCHAR(255) NOT NULL REFERENCES cameras(camera_id) ON DELETE CASCADE,
    name VARCHAR(255) NOT NULL,
    line_start_x INTEGER NOT NULL,
    line_start_y INTEGER NOT NULL,
    line_end_x INTEGER NOT NULL,
    line_end_y INTEGER NOT NULL,
    direction VARCHAR(50) DEFAULT 'both',  -- both, up, down, left, right
    enabled_classes JSONB DEFAULT '[]',  -- Array of class names to detect
    trigger_distance INTEGER DEFAULT 50,
    is_active BOOLEAN DEFAULT true,
    metadata JSONB DEFAULT '{}',
    created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX idx_line_zones_zone_id ON line_zones(zone_id);
CREATE INDEX idx_line_zones_camera ON line_zones(camera_id);
CREATE INDEX idx_line_zones_active ON line_zones(is_active);

CREATE TRIGGER update_line_zones_updated_at
    BEFORE UPDATE ON line_zones
    FOR EACH ROW
    EXECUTE FUNCTION update_updated_at_column();

-- ======================================================
-- Polygon Zones (for area monitoring)
-- ======================================================

CREATE TABLE IF NOT EXISTS polygon_zones (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    zone_id VARCHAR(255) UNIQUE NOT NULL,
    camera_id VARCHAR(255) NOT NULL REFERENCES cameras(camera_id) ON DELETE CASCADE,
    name VARCHAR(255) NOT NULL,
    polygon_points JSONB NOT NULL,  -- Array of {x, y} coordinates
    zone_type VARCHAR(50) DEFAULT 'occupancy',  -- occupancy, dwell, restricted, etc.
    enabled_classes JSONB DEFAULT '[]',
    trigger_threshold INTEGER DEFAULT 1,  -- Minimum objects to trigger
    dwell_time_seconds INTEGER,  -- For dwell zones
    is_active BOOLEAN DEFAULT true,
    metadata JSONB DEFAULT '{}',
    created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX idx_polygon_zones_zone_id ON polygon_zones(zone_id);
CREATE INDEX idx_polygon_zones_camera ON polygon_zones(camera_id);
CREATE INDEX idx_polygon_zones_type ON polygon_zones(zone_type);
CREATE INDEX idx_polygon_zones_active ON polygon_zones(is_active);

CREATE TRIGGER update_polygon_zones_updated_at
    BEFORE UPDATE ON polygon_zones
    FOR EACH ROW
    EXECUTE FUNCTION update_updated_at_column();

-- ======================================================
-- Telemetry Events (Partitioned by Time)
-- ======================================================

CREATE TABLE IF NOT EXISTS telemetry_events (
    id BIGSERIAL,
    camera_id VARCHAR(255) NOT NULL,
    tenant_id UUID,
    timestamp BIGINT NOT NULL,  -- Unix timestamp in milliseconds
    event_type INTEGER NOT NULL,  -- 0=detection, 1=tracking, 2=counting, etc.
    source_id VARCHAR(255) NOT NULL,  -- Zone ID or component ID
    properties JSONB NOT NULL DEFAULT '{}',
    frame_id BIGINT,
    created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (id, created_at)
) PARTITION BY RANGE (created_at);

-- Create indexes on the partitioned table
CREATE INDEX idx_telemetry_camera_time ON telemetry_events(camera_id, created_at);
CREATE INDEX idx_telemetry_tenant_time ON telemetry_events(tenant_id, created_at);
CREATE INDEX idx_telemetry_type ON telemetry_events(event_type);
CREATE INDEX idx_telemetry_source ON telemetry_events(source_id);
CREATE INDEX idx_telemetry_properties ON telemetry_events USING GIN (properties);

-- Initial partitions will be created by init-postgres.sql function

-- ======================================================
-- Frames (Thumbnail Storage)
-- ======================================================

CREATE TABLE IF NOT EXISTS frames (
    id BIGSERIAL PRIMARY KEY,
    camera_id VARCHAR(255) NOT NULL,
    tenant_id UUID,
    timestamp BIGINT NOT NULL,
    thumbnail BYTEA,  -- Image data
    width INTEGER,
    height INTEGER,
    encoding VARCHAR(50) DEFAULT 'jpeg',  -- jpeg, png, webp
    size_bytes INTEGER,
    created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX idx_frames_camera_time ON frames(camera_id, timestamp DESC);
CREATE INDEX idx_frames_tenant_time ON frames(tenant_id, created_at DESC);
CREATE INDEX idx_frames_created ON frames(created_at DESC);

-- ======================================================
-- Telemetry Aggregates (Pre-computed Analytics)
-- ======================================================

CREATE TABLE IF NOT EXISTS telemetry_aggregates (
    id BIGSERIAL PRIMARY KEY,
    camera_id VARCHAR(255) NOT NULL,
    tenant_id UUID,
    source_id VARCHAR(255) NOT NULL,  -- Zone or component ID
    aggregate_type VARCHAR(100) NOT NULL,  -- hourly_count, daily_average, etc.
    time_bucket TIMESTAMP WITH TIME ZONE NOT NULL,  -- Start of aggregation period
    metric_name VARCHAR(100) NOT NULL,
    metric_value DECIMAL(15,5) NOT NULL,
    sample_count INTEGER DEFAULT 1,
    metadata JSONB DEFAULT '{}',
    created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX idx_aggregates_camera_time ON telemetry_aggregates(camera_id, time_bucket DESC);
CREATE INDEX idx_aggregates_source_time ON telemetry_aggregates(source_id, time_bucket DESC);
CREATE INDEX idx_aggregates_type_time ON telemetry_aggregates(aggregate_type, time_bucket DESC);
CREATE INDEX idx_aggregates_metric ON telemetry_aggregates(metric_name);

-- ======================================================
-- Materialized Views for Dashboard Performance
-- ======================================================

-- Recent camera status (refreshed every minute via scheduled job)
CREATE MATERIALIZED VIEW IF NOT EXISTS camera_status_summary AS
SELECT
    c.camera_id,
    c.name,
    c.status,
    c.tenant_id,
    COUNT(DISTINCT te.id) as event_count_24h,
    MAX(te.created_at) as last_event_time,
    COUNT(DISTINCT lz.id) as line_zone_count,
    COUNT(DISTINCT pz.id) as polygon_zone_count
FROM cameras c
LEFT JOIN telemetry_events te ON c.camera_id = te.camera_id 
    AND te.created_at > CURRENT_TIMESTAMP - INTERVAL '24 hours'
LEFT JOIN line_zones lz ON c.camera_id = lz.camera_id AND lz.is_active = true
LEFT JOIN polygon_zones pz ON c.camera_id = pz.camera_id AND pz.is_active = true
GROUP BY c.camera_id, c.name, c.status, c.tenant_id;

CREATE UNIQUE INDEX idx_camera_status_camera_id ON camera_status_summary(camera_id);

-- ======================================================
-- Data Retention Policies
-- ======================================================

-- Function to clean up old telemetry events (run via cron)
CREATE OR REPLACE FUNCTION cleanup_old_telemetry(retention_days INTEGER DEFAULT 90)
RETURNS INTEGER AS $$
DECLARE
    deleted_count INTEGER;
BEGIN
    DELETE FROM telemetry_events
    WHERE created_at < CURRENT_TIMESTAMP - (retention_days || ' days')::INTERVAL;
    
    GET DIAGNOSTICS deleted_count = ROW_COUNT;
    RETURN deleted_count;
END;
$$ LANGUAGE plpgsql;

-- Function to clean up old frames
CREATE OR REPLACE FUNCTION cleanup_old_frames(retention_days INTEGER DEFAULT 30)
RETURNS INTEGER AS $$
DECLARE
    deleted_count INTEGER;
BEGIN
    DELETE FROM frames
    WHERE created_at < CURRENT_TIMESTAMP - (retention_days || ' days')::INTERVAL;
    
    GET DIAGNOSTICS deleted_count = ROW_COUNT;
    RETURN deleted_count;
END;
$$ LANGUAGE plpgsql;

-- ======================================================
-- Migration Metadata
-- ======================================================

CREATE TABLE IF NOT EXISTS schema_migrations (
    id SERIAL PRIMARY KEY,
    migration_name VARCHAR(255) UNIQUE NOT NULL,
    applied_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
    success BOOLEAN DEFAULT true,
    error_message TEXT
);

-- Record this migration
INSERT INTO schema_migrations (migration_name) VALUES ('001_initial_schema');

-- Log successful migration
DO $$
BEGIN
    RAISE NOTICE 'Migration 001_initial_schema completed successfully';
    RAISE NOTICE 'Core schema created with % tables', 
        (SELECT COUNT(*) FROM information_schema.tables WHERE table_schema = 'public' AND table_type = 'BASE TABLE');
END $$;

