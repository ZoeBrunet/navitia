#encoding: utf-8
from tyr.app import db
import uuid
from tyr.helper import load_instance_config


class User(db.Model):
    __table_args__ = {"schema": "jormungandr"}
    id = db.Column(db.Integer, primary_key=True)
    login = db.Column(db.Text, unique=True)
    email = db.Column(db.Text, unique=True)
    keys = db.relationship('Key', backref='user', lazy='dynamic')

    authorizations = db.relationship('Authorization', backref='user',
            lazy='dynamic')

    def __init__(self, login=None, email=None, keys=None, authorizations=None):
        self.login = login
        self.email = email
        if keys:
            self.keys = keys
        if authorizations:
            self.authorizations = authorizations

    def __repr__(self):
        return '<User %r>' % self.email

    def add_key(self, valid_until=None):
        """
        génére une nouvelle clé pour l'utilisateur
        et l'ajoute à sa liste de clé
        c'est à l'appelant de commit la transaction
        :return la clé généré
        """
        key = Key(valid_until=valid_until)
        key.token = str(uuid.uuid4())
        self.keys.append(key)
        db.session.add(key)
        return key


class Key(db.Model):
    __table_args__ = {"schema": "jormungandr"}
    id = db.Column(db.Integer, primary_key=True)
    token = db.Column(db.Text, unique=True)
    user_id = db.Column(db.Integer, db.ForeignKey('jormungandr.user.id'))
    valid_until = db.Column(db.Date)

    def __init__(self, token=None, user_id=None, valid_until=None):
        self.token = token
        self.user_id = user_id
        self.valid_until = valid_until

    def __repr__(self):
        return '<Key %r>' % self.token


class Instance(db.Model):
    __table_args__ = {"schema": "jormungandr"}
    id = db.Column(db.Integer, primary_key=True)
    name = db.Column(db.Text, unique=True)
    is_free = db.Column(db.Boolean)

    authorizations = db.relationship('Authorization', backref='instance',
            lazy='dynamic')

    jobs = db.relationship('Job', backref='instance', lazy='dynamic')

    def __init__(self, name=None, is_free=False, authorizations=None):
        self.name = name
        self.is_free = is_free
        if authorizations:
            self.authorizations = authorizations

        self.source_directory = None
        self.backup_directory = None
        self.tmp_file = None
        self.target_file = None
        self.rt_topics = None
        self.exchange = None
        self.synonyms_file = None
        self.aliases_file = None

        self.pg_host = None
        self.pg_dbname = None
        self.pg_username = None
        self.pg_password = None


    def load_technical_config(self):
        config = load_instance_config(self.name)
        self.source_directory = config['instance']['source-directory']
        self.backup_directory = config['instance']['backup-directory']
        self.tmp_file = config['instance']['tmp-file']
        self.target_file = config['instance']['target-file']
        self.rt_topics = config['instance']['rt-topics']
        self.exchange = config['instance']['exchange']
        self.synonyms_file = config['instance']['synonyms_file']
        self.aliases_file = config['instance']['aliases_file']

        self.pg_host = config['database']['host']
        self.pg_dbname = config['database']['dbname']
        self.pg_username = config['database']['username']
        self.pg_password = config['database']['password']


    def __repr__(self):
        return '<Instance %r>' % self.name


class Api(db.Model):
    __table_args__ = {"schema": "jormungandr"}
    id = db.Column(db.Integer, primary_key=True)
    name = db.Column(db.Text, unique=True)

    authorizations = db.relationship('Authorization', backref='api',
            lazy='dynamic')

    def __init__(self, name=None):
        self.name = name

    def __repr__(self):
        return '<Api %r>' % self.name


class Authorization(db.Model):
    __table_args__ = {"schema": "jormungandr"}
    user_id = db.Column(db.Integer, db.ForeignKey('jormungandr.user.id'),
            primary_key=True)
    instance_id = db.Column(db.Integer,
            db.ForeignKey('jormungandr.instance.id'), primary_key=True)
    api_id = db.Column(db.Integer,
            db.ForeignKey('jormungandr.api.id'), primary_key=True)

    def __init__(self, user_id=None, instance_id=None, api_id=None):
        self.user_id = user_id
        self.instance_id = instance_id
        self.api_id = api_id

    def __repr__(self):
        return '<Authorization %r-%r-%r>' \
                % (self.user_id, self.instance_id, self.api_id)


class Job(db.Model):
    __table_args__ = {"schema": "tyr"}
    id = db.Column(db.Integer, primary_key=True)
    task_uuid = db.Column(db.Text)
    filename = db.Column(db.Text)
    type = db.Column(db.Text)
    instance_id = db.Column(db.Integer, db.ForeignKey('jormungandr.instance.id'))


