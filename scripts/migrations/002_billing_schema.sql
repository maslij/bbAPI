-- BrinkByte Vision - tAPI Billing Schema Migration
-- Migration 002: Billing, Licensing, and Usage Tracking
-- This adds tables for integration with the cloud billing service

-- ======================================================
-- Edge Device Registration
-- ======================================================

CREATE TABLE IF NOT EXISTS edge_devices (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    device_id VARCHAR(255) UNIQUE NOT NULL,  -- Hardware UUID
    tenant_id UUID NOT NULL,
    name VARCHAR(255),
    status VARCHAR(50) DEFAULT 'active',  -- active, suspended, offline, decommissioned
    management_tier VARCHAR(50) DEFAULT 'basic',  -- basic ($50/mo), managed ($65/mo)
    last_heartbeat TIMESTAMP WITH TIME ZONE,
    heartbeat_interval_seconds INTEGER DEFAULT 900,  -- 15 minutes
    hardware_info JSONB DEFAULT '{}',  -- CPU, memory, disk, GPU info
    network_info JSONB DEFAULT '{}',  -- IP, location, bandwidth
    software_version VARCHAR(100),
    metadata JSONB DEFAULT '{}',
    created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX idx_edge_devices_device_id ON edge_devices(device_id);
CREATE INDEX idx_edge_devices_tenant ON edge_devices(tenant_id);
CREATE INDEX idx_edge_devices_status ON edge_devices(status);
CREATE INDEX idx_edge_devices_last_heartbeat ON edge_devices(last_heartbeat);

CREATE TRIGGER update_edge_devices_updated_at
    BEFORE UPDATE ON edge_devices
    FOR EACH ROW
    EXECUTE FUNCTION update_updated_at_column();

-- ======================================================
-- Camera Licenses (Cached from Billing Service)
-- ======================================================

CREATE TABLE IF NOT EXISTS camera_licenses (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    camera_id VARCHAR(255) NOT NULL,
    tenant_id UUID NOT NULL,
    license_mode VARCHAR(50) DEFAULT 'trial',  -- trial, base, unlicensed
    is_trial BOOLEAN DEFAULT true,
    trial_start_date TIMESTAMP WITH TIME ZONE,
    trial_end_date TIMESTAMP WITH TIME ZONE,
    subscription_start_date TIMESTAMP WITH TIME ZONE,
    subscription_end_date TIMESTAMP WITH TIME ZONE,
    enabled_growth_packs JSONB DEFAULT '[]',  -- Array of pack names
    status VARCHAR(50) DEFAULT 'active',  -- active, suspended, expired, grace_period
    grace_period_end TIMESTAMP WITH TIME ZONE,  -- End of grace period before suspension
    last_validated TIMESTAMP WITH TIME ZONE,
    cached_until TIMESTAMP WITH TIME ZONE,
    validation_source VARCHAR(50) DEFAULT 'billing_service',  -- billing_service, cache, offline_mode
    billing_service_response JSONB DEFAULT '{}',  -- Full response from billing service
    metadata JSONB DEFAULT '{}',
    created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(camera_id, tenant_id)
);

CREATE INDEX idx_camera_licenses_camera ON camera_licenses(camera_id);
CREATE INDEX idx_camera_licenses_tenant ON camera_licenses(tenant_id);
CREATE INDEX idx_camera_licenses_status ON camera_licenses(status);
CREATE INDEX idx_camera_licenses_mode ON camera_licenses(license_mode);
CREATE INDEX idx_camera_licenses_cached_until ON camera_licenses(cached_until);
CREATE INDEX idx_camera_licenses_trial_end ON camera_licenses(trial_end_date) WHERE is_trial = true;

CREATE TRIGGER update_camera_licenses_updated_at
    BEFORE UPDATE ON camera_licenses
    FOR EACH ROW
    EXECUTE FUNCTION update_updated_at_column();

-- ======================================================
-- Feature Entitlements Cache
-- ======================================================

CREATE TABLE IF NOT EXISTS feature_entitlements (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    tenant_id UUID NOT NULL,
    feature_category VARCHAR(100) NOT NULL,  -- cv_models, analytics, outputs, agents, llm
    feature_name VARCHAR(255) NOT NULL,
    is_enabled BOOLEAN DEFAULT false,
    quota INTEGER DEFAULT -1,  -- -1 for unlimited, 0 for disabled, >0 for limited
    quota_used INTEGER DEFAULT 0,
    quota_period VARCHAR(50) DEFAULT 'monthly',  -- monthly, daily, per_request
    quota_reset_date TIMESTAMP WITH TIME ZONE,
    growth_pack_source VARCHAR(100),  -- Which growth pack enables this feature
    cached_until TIMESTAMP WITH TIME ZONE,
    validation_source VARCHAR(50) DEFAULT 'billing_service',
    metadata JSONB DEFAULT '{}',
    created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(tenant_id, feature_category, feature_name)
);

CREATE INDEX idx_entitlements_tenant_category ON feature_entitlements(tenant_id, feature_category);
CREATE INDEX idx_entitlements_feature ON feature_entitlements(feature_name);
CREATE INDEX idx_entitlements_enabled ON feature_entitlements(is_enabled);
CREATE INDEX idx_entitlements_cached_until ON feature_entitlements(cached_until);
CREATE INDEX idx_entitlements_growth_pack ON feature_entitlements(growth_pack_source);

CREATE TRIGGER update_feature_entitlements_updated_at
    BEFORE UPDATE ON feature_entitlements
    FOR EACH ROW
    EXECUTE FUNCTION update_updated_at_column();

-- ======================================================
-- Local Usage Tracking (Before Sync to Billing Service)
-- ======================================================

CREATE TABLE IF NOT EXISTS usage_events (
    id BIGSERIAL,
    tenant_id UUID NOT NULL,
    device_id VARCHAR(255),
    event_type VARCHAR(100) NOT NULL,  -- api_call, llm_token, storage_gb_day, sms, agent_execution, etc.
    resource_id VARCHAR(255),  -- Camera ID, agent ID, endpoint, etc.
    resource_type VARCHAR(100),  -- camera, agent, api_endpoint, llm_seat
    quantity DECIMAL(15,5) NOT NULL DEFAULT 1,
    unit VARCHAR(50) NOT NULL,  -- calls, tokens, GB-days, messages, executions
    cost DECIMAL(10,4),  -- Calculated locally if possible
    billable BOOLEAN DEFAULT true,
    metadata JSONB DEFAULT '{}',  -- Additional context (endpoint name, model name, etc.)
    event_time TIMESTAMP WITH TIME ZONE NOT NULL DEFAULT CURRENT_TIMESTAMP,
    synced_to_billing BOOLEAN DEFAULT false,
    sync_attempt_count INTEGER DEFAULT 0,
    last_sync_attempt TIMESTAMP WITH TIME ZONE,
    sync_error_message TEXT,
    billing_service_id UUID,  -- ID assigned by billing service after sync
    created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (id, event_time)
) PARTITION BY RANGE (event_time);

CREATE INDEX idx_usage_events_tenant_time ON usage_events(tenant_id, event_time DESC);
CREATE INDEX idx_usage_events_type_time ON usage_events(event_type, event_time DESC);
CREATE INDEX idx_usage_events_resource ON usage_events(resource_id, event_time DESC);
CREATE INDEX idx_usage_events_synced ON usage_events(synced_to_billing, event_time) WHERE synced_to_billing = false;
CREATE INDEX idx_usage_events_device ON usage_events(device_id, event_time DESC);
CREATE INDEX idx_usage_events_metadata ON usage_events USING GIN (metadata);

-- Initial partitions will be created by init-postgres.sql function

-- ======================================================
-- Billing Sync Status Tracking
-- ======================================================

CREATE TABLE IF NOT EXISTS billing_sync_status (
    id SERIAL PRIMARY KEY,
    sync_type VARCHAR(100) NOT NULL,  -- license_validation, usage_upload, entitlement_refresh, heartbeat
    last_sync_time TIMESTAMP WITH TIME ZONE,
    last_sync_status VARCHAR(50),  -- success, failed, partial, in_progress
    records_processed INTEGER DEFAULT 0,
    records_synced INTEGER DEFAULT 0,
    records_failed INTEGER DEFAULT 0,
    error_message TEXT,
    error_details JSONB DEFAULT '{}',
    next_sync_scheduled TIMESTAMP WITH TIME ZONE,
    sync_duration_ms INTEGER,  -- How long the sync took
    metadata JSONB DEFAULT '{}',
    created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX idx_sync_status_type ON billing_sync_status(sync_type);
CREATE INDEX idx_sync_status_time ON billing_sync_status(last_sync_time DESC);
CREATE INDEX idx_sync_status_status ON billing_sync_status(last_sync_status);
CREATE INDEX idx_sync_status_next_scheduled ON billing_sync_status(next_sync_scheduled);

CREATE TRIGGER update_billing_sync_status_updated_at
    BEFORE UPDATE ON billing_sync_status
    FOR EACH ROW
    EXECUTE FUNCTION update_updated_at_column();

-- ======================================================
-- License Validation Audit Log
-- ======================================================

CREATE TABLE IF NOT EXISTS license_validation_log (
    id BIGSERIAL PRIMARY KEY,
    camera_id VARCHAR(255) NOT NULL,
    tenant_id UUID NOT NULL,
    validation_time TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
    validation_result VARCHAR(50) NOT NULL,  -- valid, invalid, expired, grace_period, offline_cached
    validation_source VARCHAR(50) NOT NULL,  -- billing_service, redis_cache, postgres_cache, offline_mode
    license_mode VARCHAR(50),
    enabled_growth_packs JSONB DEFAULT '[]',
    cache_hit BOOLEAN DEFAULT false,
    response_time_ms INTEGER,
    error_message TEXT,
    billing_service_response JSONB DEFAULT '{}',
    metadata JSONB DEFAULT '{}',
    created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX idx_validation_log_camera ON license_validation_log(camera_id, validation_time DESC);
CREATE INDEX idx_validation_log_tenant ON license_validation_log(tenant_id, validation_time DESC);
CREATE INDEX idx_validation_log_result ON license_validation_log(validation_result);
CREATE INDEX idx_validation_log_time ON license_validation_log(validation_time DESC);

-- ======================================================
-- Growth Pack Feature Mapping (Configuration)
-- ======================================================

CREATE TABLE IF NOT EXISTS growth_pack_features (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    growth_pack_name VARCHAR(100) NOT NULL,
    growth_pack_type VARCHAR(50) NOT NULL,  -- base, advanced_analytics, intelligence, industry, integration
    feature_category VARCHAR(100) NOT NULL,
    feature_name VARCHAR(255) NOT NULL,
    is_included BOOLEAN DEFAULT true,
    quota_multiplier DECIMAL(10,2) DEFAULT 1.0,  -- For quotas that scale with growth packs
    description TEXT,
    created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(growth_pack_name, feature_category, feature_name)
);

CREATE INDEX idx_growth_pack_features_pack ON growth_pack_features(growth_pack_name);
CREATE INDEX idx_growth_pack_features_type ON growth_pack_features(growth_pack_type);
CREATE INDEX idx_growth_pack_features_feature ON growth_pack_features(feature_category, feature_name);

-- Insert base license features
INSERT INTO growth_pack_features (growth_pack_name, growth_pack_type, feature_category, feature_name, description) VALUES
-- Base Models ($60/cam/mo)
('base', 'base', 'cv_models', 'person', 'Person detection'),
('base', 'base', 'cv_models', 'car', 'Car detection'),
('base', 'base', 'cv_models', 'van', 'Van detection'),
('base', 'base', 'cv_models', 'truck', 'Truck detection'),
('base', 'base', 'cv_models', 'bus', 'Bus detection'),
('base', 'base', 'cv_models', 'motorcycle', 'Motorcycle detection'),

-- Base Analytics
('base', 'base', 'analytics', 'detection', 'Object detection'),
('base', 'base', 'analytics', 'tracking', 'Object tracking'),
('base', 'base', 'analytics', 'counting', 'Object counting'),
('base', 'base', 'analytics', 'dwell', 'Dwell time analysis'),
('base', 'base', 'analytics', 'heatmap', 'Heatmap generation'),
('base', 'base', 'analytics', 'direction', 'Direction detection'),
('base', 'base', 'analytics', 'speed', 'Speed estimation'),
('base', 'base', 'analytics', 'privacy_mask', 'Privacy masking'),

-- Base Outputs
('base', 'base', 'outputs', 'edge_io', 'Edge device I/O'),
('base', 'base', 'outputs', 'dashboard', 'Dashboard access'),
('base', 'base', 'outputs', 'email', 'Email notifications'),
('base', 'base', 'outputs', 'webhook', 'Webhook integration'),
('base', 'base', 'outputs', 'api', 'API access'),

-- Advanced Analytics Pack ($20/cam/mo)
('advanced_analytics', 'advanced_analytics', 'analytics', 'near_miss', 'Near-miss detection'),
('advanced_analytics', 'advanced_analytics', 'analytics', 'interaction_time', 'Interaction time tracking'),
('advanced_analytics', 'advanced_analytics', 'analytics', 'queue_counter', 'Queue counting'),
('advanced_analytics', 'advanced_analytics', 'analytics', 'object_size', 'Object size estimation'),

-- Active Transport Pack (Industry Pack)
('active_transport', 'industry', 'cv_models', 'bike', 'Bicycle detection'),
('active_transport', 'industry', 'cv_models', 'scooter', 'Scooter detection'),
('active_transport', 'industry', 'cv_models', 'pram', 'Pram/stroller detection'),
('active_transport', 'industry', 'cv_models', 'wheelchair', 'Wheelchair detection'),

-- Intelligence Pack ($400/tenant/mo)
('intelligence', 'intelligence', 'llm', 'analyst_seat_full', 'Full analyst seat with advanced features'),
('intelligence', 'intelligence', 'llm', 'premium_connectors', 'Premium data connectors'),
('intelligence', 'intelligence', 'llm', 'automated_reports', 'Automated report generation'),

-- Integration Pack
('integration', 'integration', 'outputs', 'sms', 'SMS notifications'),
('integration', 'integration', 'outputs', 'cloud_export', 'Cloud storage export'),
('integration', 'integration', 'outputs', 'vms_connectors', 'VMS system connectors');

-- ======================================================
-- Materialized Views for Billing Dashboard
-- ======================================================

-- Current tenant usage summary (refresh every 5 minutes)
CREATE MATERIALIZED VIEW IF NOT EXISTS tenant_usage_summary AS
SELECT
    tenant_id,
    event_type,
    DATE_TRUNC('day', event_time) as usage_date,
    COUNT(*) as event_count,
    SUM(quantity) as total_quantity,
    SUM(cost) as total_cost,
    COUNT(*) FILTER (WHERE synced_to_billing = false) as pending_sync_count
FROM usage_events
WHERE event_time > CURRENT_TIMESTAMP - INTERVAL '30 days'
GROUP BY tenant_id, event_type, DATE_TRUNC('day', event_time);

CREATE INDEX idx_tenant_usage_summary_tenant ON tenant_usage_summary(tenant_id, usage_date DESC);

-- Active camera licenses per tenant
CREATE MATERIALIZED VIEW IF NOT EXISTS tenant_license_summary AS
SELECT
    tenant_id,
    license_mode,
    COUNT(*) as camera_count,
    COUNT(*) FILTER (WHERE status = 'active') as active_count,
    COUNT(*) FILTER (WHERE status = 'suspended') as suspended_count,
    COUNT(*) FILTER (WHERE is_trial = true) as trial_count,
    MIN(trial_end_date) as earliest_trial_end,
    array_agg(DISTINCT jsonb_array_elements_text(enabled_growth_packs)) as all_growth_packs
FROM camera_licenses
GROUP BY tenant_id, license_mode;

CREATE INDEX idx_tenant_license_summary_tenant ON tenant_license_summary(tenant_id);

-- ======================================================
-- Helper Functions
-- ======================================================

-- Function to check if a feature is enabled for a tenant
CREATE OR REPLACE FUNCTION is_feature_enabled(
    p_tenant_id UUID,
    p_feature_category VARCHAR(100),
    p_feature_name VARCHAR(255)
)
RETURNS BOOLEAN AS $$
DECLARE
    v_enabled BOOLEAN;
BEGIN
    SELECT is_enabled INTO v_enabled
    FROM feature_entitlements
    WHERE tenant_id = p_tenant_id
      AND feature_category = p_feature_category
      AND feature_name = p_feature_name
      AND (cached_until IS NULL OR cached_until > CURRENT_TIMESTAMP);
    
    RETURN COALESCE(v_enabled, false);
END;
$$ LANGUAGE plpgsql;

-- Function to get camera license status
CREATE OR REPLACE FUNCTION get_camera_license_status(p_camera_id VARCHAR(255))
RETURNS TABLE (
    is_valid BOOLEAN,
    license_mode VARCHAR(50),
    status VARCHAR(50),
    enabled_growth_packs JSONB,
    is_cached BOOLEAN
) AS $$
BEGIN
    RETURN QUERY
    SELECT
        (cl.status = 'active' AND (cl.cached_until IS NULL OR cl.cached_until > CURRENT_TIMESTAMP)) as is_valid,
        cl.license_mode,
        cl.status,
        cl.enabled_growth_packs,
        (cl.cached_until IS NOT NULL AND cl.cached_until > CURRENT_TIMESTAMP) as is_cached
    FROM camera_licenses cl
    WHERE cl.camera_id = p_camera_id
    LIMIT 1;
END;
$$ LANGUAGE plpgsql;

-- Function to mark usage events as synced
CREATE OR REPLACE FUNCTION mark_usage_synced(p_event_ids BIGINT[], p_billing_service_ids UUID[])
RETURNS INTEGER AS $$
DECLARE
    v_updated_count INTEGER;
BEGIN
    WITH updated AS (
        UPDATE usage_events
        SET 
            synced_to_billing = true,
            billing_service_id = p_billing_service_ids[array_position(p_event_ids, id)],
            sync_attempt_count = sync_attempt_count + 1,
            last_sync_attempt = CURRENT_TIMESTAMP
        WHERE id = ANY(p_event_ids)
        RETURNING 1
    )
    SELECT COUNT(*) INTO v_updated_count FROM updated;
    
    RETURN v_updated_count;
END;
$$ LANGUAGE plpgsql;

-- ======================================================
-- Migration Metadata
-- ======================================================

INSERT INTO schema_migrations (migration_name) VALUES ('002_billing_schema');

-- Log successful migration
DO $$
BEGIN
    RAISE NOTICE 'Migration 002_billing_schema completed successfully';
    RAISE NOTICE 'Billing and licensing schema created';
    RAISE NOTICE 'Total growth pack features: %', 
        (SELECT COUNT(*) FROM growth_pack_features);
END $$;

