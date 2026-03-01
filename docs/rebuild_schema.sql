-- Rebuild chat schema from scratch.
-- WARNING: This script drops existing chat tables and all data in those tables.

CREATE DATABASE IF NOT EXISTS chat DEFAULT CHARACTER SET utf8mb4;
USE chat;

SET FOREIGN_KEY_CHECKS = 0;

DROP TABLE IF EXISTS offlinemessage;
DROP TABLE IF EXISTS groupuser;
DROP TABLE IF EXISTS friend;
DROP TABLE IF EXISTS allgroup;
DROP TABLE IF EXISTS user;

SET FOREIGN_KEY_CHECKS = 1;

CREATE TABLE user (
    id INT PRIMARY KEY AUTO_INCREMENT,
    name VARCHAR(50) NOT NULL,
    password VARCHAR(50) NOT NULL,
    state VARCHAR(20) NOT NULL DEFAULT 'offline'
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE friend (
    userid INT NOT NULL,
    friendid INT NOT NULL,
    PRIMARY KEY (userid, friendid),
    CONSTRAINT fk_friend_userid FOREIGN KEY (userid) REFERENCES user(id) ON DELETE CASCADE,
    CONSTRAINT fk_friend_friendid FOREIGN KEY (friendid) REFERENCES user(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE allgroup (
    id INT PRIMARY KEY AUTO_INCREMENT,
    groupname VARCHAR(100) NOT NULL,
    groupdesc VARCHAR(255) NOT NULL DEFAULT ''
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE groupuser (
    groupid INT NOT NULL,
    userid INT NOT NULL,
    grouprole VARCHAR(32) NOT NULL DEFAULT 'normal',
    PRIMARY KEY (groupid, userid),
    CONSTRAINT fk_groupuser_groupid FOREIGN KEY (groupid) REFERENCES allgroup(id) ON DELETE CASCADE,
    CONSTRAINT fk_groupuser_userid FOREIGN KEY (userid) REFERENCES user(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE offlinemessage (
    userid INT NOT NULL,
    message VARCHAR(1024) NOT NULL,
    INDEX idx_offlinemessage_userid (userid),
    CONSTRAINT fk_offlinemessage_userid FOREIGN KEY (userid) REFERENCES user(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
