WITH
    Q1
    AS (SELECT friendlyName
             , batVoltage
             , batPercentage
             , coulombCount
             , batPercentageCoulomb
             , timestamp
        FROM deviceStatusLog
        WHERE timestamp >= '2025-04-01 12:56:41'
        ORDER BY timestamp, friendlyName),
    Q2
    AS (SELECT friendlyName
             , MIN(timestamp) as min_timestamp
             , MAX(timestamp) as max_timestamp
          FROM Q1
         GROUP BY friendlyName)
SELECT Q1.*
     , ((JULIANDAY(Q1.timestamp) - JULIANDAY(Q2.min_timestamp)) /
        (JULIANDAY(Q2.max_timestamp) - JULIANDAY(Q2.min_timestamp))) as pot
  FROM Q1
    JOIN Q2 ON Q1.friendlyName = Q2.friendlyName
;



WITH
    Q1
    AS (SELECT friendlyName
             , batVoltage
             , batPercentage
             , batPercentageCoulomb
             , coulombCount
             , timestamp
        FROM deviceStatusLog
        WHERE timestamp >= '2025-04-01 12:56:41'
        ORDER BY timestamp, friendlyName)
SELECT MIN(batVoltage) as minBatVoltage
     , AVG(batVoltage) as avgBatVoltage
     , MAX(batVoltage) as maxBatVoltage
  FROM Q1
 WHERE round(coulombCount,0) = -100